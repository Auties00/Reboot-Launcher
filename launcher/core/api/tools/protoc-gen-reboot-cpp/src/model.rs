use std::collections::HashMap;

use prost_reflect::{DescriptorPool, DynamicMessage, ExtensionDescriptor, FileDescriptor};
use prost_types::field_descriptor_proto::{Label, Type};

pub const PACKAGE: &str = "reboot.api.v1";
pub const OPTIONS_FILE: &str = "reboot/api/v1/options.proto";

pub struct Field {
    pub name: String,
    pub number: i32,
    pub repeated: bool,
    pub ty: Type,
    pub type_name: String,
    pub oneof_index: Option<i32>,
    pub proto3_optional: bool,
    pub fixed_size: u32,
    pub deprecated: bool,
    pub path: Vec<i32>,
}

impl Field {
    pub fn in_real_oneof(&self) -> bool {
        self.oneof_index.is_some() && !self.proto3_optional
    }

    // Real oneof members and proto3 `optional` fields both have explicit presence.
    pub fn optional(&self) -> bool {
        self.oneof_index.is_some()
    }
}

pub struct Message {
    pub name: String,
    pub fields: Vec<Field>,
    pub has_nested: bool,
    pub oneofs: Vec<String>,
    pub path: Vec<i32>,
}

impl Message {
    pub fn oneof_groups(&self) -> Vec<Vec<&Field>> {
        let mut groups: Vec<(i32, Vec<&Field>)> = Vec::new();
        for field in self.fields.iter().filter(|field| field.in_real_oneof()) {
            let index = field.oneof_index.unwrap_or_default();
            match groups.iter_mut().find(|(i, _)| *i == index) {
                Some((_, members)) => members.push(field),
                None => groups.push((index, vec![field])),
            }
        }
        groups.sort_by_key(|(index, _)| *index);
        groups.into_iter().map(|(_, members)| members).collect()
    }
}

pub struct EnumValue {
    pub name: String,
    pub number: i32,
    pub path: Vec<i32>,
}

pub struct Enum {
    pub name: String,
    pub values: Vec<EnumValue>,
    pub path: Vec<i32>,
}

pub struct MethodInfo {
    pub id: u32,
    pub kind: i32,
    pub progress: i32,
    pub disconnect: i32,
}

pub struct Method {
    pub name: String,
    pub input_type: String,
    pub output_type: String,
    pub streaming: bool,
    pub info: Option<MethodInfo>,
    pub path: Vec<i32>,
}

pub struct Service {
    pub name: String,
    pub id: u32,
    pub methods: Vec<Method>,
    pub path: Vec<i32>,
}

pub struct File {
    pub name: String,
    pub package: String,
    pub dependencies: Vec<String>,
    pub syntax: String,
    pub messages: Vec<Message>,
    pub enums: Vec<Enum>,
    pub services: Vec<Service>,
    pub comments: HashMap<Vec<i32>, String>,
}

impl File {
    pub fn stem(&self) -> &str {
        let base = self.name.rsplit('/').next().unwrap_or(&self.name);
        base.strip_suffix(".proto").unwrap_or(base)
    }
}

// The custom options declared in options.proto.
struct Extensions {
    fixed_size: ExtensionDescriptor,
    method_info: ExtensionDescriptor,
    service_info: ExtensionDescriptor,
}

impl Extensions {
    fn find(pool: &DescriptorPool) -> Result<Self, String> {
        let get = |name: &str| {
            pool.get_extension_by_name(&format!("{PACKAGE}.{name}"))
                .ok_or_else(|| format!("{OPTIONS_FILE} does not declare {name}"))
        };
        Ok(Self { fixed_size: get("fixed_size")?, method_info: get("method_info")?, service_info: get("service_info")? })
    }
}

fn extension_message(options: &DynamicMessage, extension: &ExtensionDescriptor) -> Option<DynamicMessage> {
    if !options.has_extension(extension) {
        return None;
    }
    options.get_extension(extension).as_message().cloned()
}

fn u32_field(message: &DynamicMessage, name: &str) -> u32 {
    message.get_field_by_name(name).and_then(|value| value.as_u32()).unwrap_or_default()
}

fn enum_field(message: &DynamicMessage, name: &str) -> i32 {
    message.get_field_by_name(name).and_then(|value| value.as_enum_number()).unwrap_or_default()
}

fn child_path(parent: &[i32], kind: i32, index: usize) -> Vec<i32> {
    let mut path = parent.to_vec();
    path.extend([kind, index as i32]);
    path
}

// FileDescriptorProto field numbers, used as source-location paths.
const MESSAGE_TYPE: i32 = 4;
const ENUM_TYPE: i32 = 5;
const SERVICE: i32 = 6;
const MESSAGE_FIELD: i32 = 2;
const ENUM_VALUE: i32 = 2;
const SERVICE_METHOD: i32 = 2;

fn load_message(pool: &DescriptorPool, ext: &Extensions, package: &str, proto: &prost_types::DescriptorProto,
                path: Vec<i32>) -> Message {
    let full_name = if package.is_empty() { proto.name().to_owned() } else { format!("{package}.{}", proto.name()) };
    let descriptor = pool.get_message_by_name(&full_name);
    let fields = proto.field.iter().enumerate().map(|(index, field)| {
        let options = descriptor.as_ref().and_then(|d| d.get_field_by_name(field.name())).map(|f| f.options());
        let fixed_size = options.as_ref()
            .filter(|o| o.has_extension(&ext.fixed_size))
            .and_then(|o| o.get_extension(&ext.fixed_size).as_u32())
            .unwrap_or_default();
        let deprecated = options.as_ref()
            .and_then(|o| o.get_field_by_name("deprecated"))
            .and_then(|value| value.as_bool())
            .unwrap_or_default();
        Field {
            name: field.name().to_owned(),
            number: field.number(),
            repeated: field.label() == Label::Repeated,
            ty: field.r#type(),
            type_name: field.type_name().to_owned(),
            oneof_index: field.oneof_index,
            proto3_optional: field.proto3_optional(),
            fixed_size,
            deprecated,
            path: child_path(&path, MESSAGE_FIELD, index),
        }
    }).collect();
    Message {
        name: proto.name().to_owned(),
        fields,
        has_nested: !proto.nested_type.is_empty() || !proto.enum_type.is_empty(),
        oneofs: proto.oneof_decl.iter().map(|oneof| oneof.name().to_owned()).collect(),
        path,
    }
}

fn load_file(pool: &DescriptorPool, ext: &Extensions, file: &FileDescriptor) -> File {
    let proto = file.file_descriptor_proto();
    let package = proto.package().to_owned();
    let messages = proto.message_type.iter().enumerate()
        .map(|(i, m)| load_message(pool, ext, &package, m, vec![MESSAGE_TYPE, i as i32]))
        .collect();
    let enums = proto.enum_type.iter().enumerate().map(|(i, e)| {
        let path = vec![ENUM_TYPE, i as i32];
        Enum {
            name: e.name().to_owned(),
            values: e.value.iter().enumerate().map(|(j, v)| EnumValue {
                name: v.name().to_owned(),
                number: v.number(),
                path: child_path(&path, ENUM_VALUE, j),
            }).collect(),
            path,
        }
    }).collect();
    let services = file.services().enumerate().map(|(i, service)| {
        let path = vec![SERVICE, i as i32];
        let id = extension_message(&service.options(), &ext.service_info).map(|info| u32_field(&info, "id"));
        Service {
            name: service.name().to_owned(),
            id: id.unwrap_or_default(),
            methods: service.methods().enumerate().map(|(j, method)| {
                let proto = method.method_descriptor_proto();
                Method {
                    name: method.name().to_owned(),
                    input_type: proto.input_type().to_owned(),
                    output_type: proto.output_type().to_owned(),
                    streaming: proto.client_streaming() || proto.server_streaming(),
                    info: extension_message(&method.options(), &ext.method_info).map(|info| MethodInfo {
                        id: u32_field(&info, "id"),
                        kind: enum_field(&info, "kind"),
                        progress: enum_field(&info, "progress"),
                        disconnect: enum_field(&info, "disconnect"),
                    }),
                    path: child_path(&path, SERVICE_METHOD, j),
                }
            }).collect(),
            path,
        }
    }).collect();
    let comments = proto.source_code_info.iter()
        .flat_map(|info| info.location.iter())
        .filter(|location| !location.leading_comments().is_empty())
        .map(|location| (location.path.clone(), location.leading_comments().to_owned()))
        .collect();
    File {
        name: proto.name().to_owned(),
        package,
        dependencies: proto.dependency.clone(),
        syntax: proto.syntax.clone().unwrap_or_else(|| "proto2".to_owned()),
        messages,
        enums,
        services,
        comments,
    }
}

// Every file in the pool, in protoc's order (dependencies first).
pub fn load(pool: &DescriptorPool) -> Result<Vec<File>, String> {
    let ext = Extensions::find(pool)?;
    Ok(pool.files().map(|file| load_file(pool, &ext, &file)).collect())
}
