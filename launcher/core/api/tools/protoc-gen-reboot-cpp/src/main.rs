//! Generates the C++ side of reboot.api.v1: aggregates, the method table, handler interfaces and
//! dispatch, plus the method-id header of the public C ABI. Each generated header holds every type of one .proto file, the one exception to the
//! one-main-type-per-header rule.
//!
//! As a protoc plugin it reads a CodeGeneratorRequest on stdin. Standalone it runs protoc itself:
//!
//!     protoc-gen-reboot-cpp --protoc <protoc> --proto-root <launcher/schema/proto> --out <launcher/core> [--check]
//!
//! --check writes nothing and fails when the committed output differs from a fresh run.
//!
//! The aggregates encode with the sb codec, where field number = declaration index + 1, so every
//! message numbers its fields 1..N in declaration order (the contiguity rule). A oneof becomes
//! consecutive std::optional members; check_cases, which decode runs, requires exactly one of them
//! set, and none set is a case added by a newer schema.

mod cpp;
mod model;

use std::collections::BTreeMap;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};

use prost::Message as _;
use prost_reflect::DescriptorPool;
use prost_types::compiler::code_generator_response::{Feature, File as OutputFile};
use prost_types::compiler::CodeGeneratorResponse;

// CodeGeneratorRequest with proto_file left encoded: prost-reflect sees custom options only in
// descriptors it decodes itself.
#[derive(Clone, PartialEq, prost::Message)]
struct PluginRequest {
    #[prost(string, repeated, tag = "1")]
    file_to_generate: Vec<String>,
    #[prost(bytes = "vec", repeated, tag = "15")]
    proto_file: Vec<Vec<u8>>,
}

fn generate(pool: &DescriptorPool, to_generate: &[String]) -> Result<BTreeMap<String, String>, String> {
    let files = model::load(pool)?;
    cpp::Generator::new(&files, to_generate)?.run()
}

fn run_plugin() -> Result<(), String> {
    let mut input = Vec::new();
    std::io::stdin().read_to_end(&mut input).map_err(|e| format!("reading the request: {e}"))?;
    let request = PluginRequest::decode(input.as_slice()).map_err(|e| format!("decoding the request: {e}"))?;
    let mut pool = DescriptorPool::new();
    let outputs = request.proto_file.iter()
        .try_for_each(|file| pool.decode_file_descriptor_proto(file.as_slice()).map_err(|e| e.to_string()))
        .and_then(|()| generate(&pool, &request.file_to_generate));
    let response = match outputs {
        Ok(outputs) => CodeGeneratorResponse {
            supported_features: Some(Feature::Proto3Optional as u64),
            file: outputs.into_iter()
                .map(|(name, content)| OutputFile { name: Some(name), content: Some(content), ..Default::default() })
                .collect(),
            ..Default::default()
        },
        Err(error) => CodeGeneratorResponse { error: Some(error), ..Default::default() },
    };
    std::io::stdout().write_all(&response.encode_to_vec()).map_err(|e| format!("writing the response: {e}"))
}

struct Options {
    protoc: String,
    proto_root: PathBuf,
    out: PathBuf,
    check: bool,
}

fn parse_options(args: &[String]) -> Result<Options, String> {
    let mut protoc = "protoc".to_owned();
    let mut proto_root = None;
    let mut out = None;
    let mut check = false;
    let mut iter = args.iter();
    while let Some(arg) = iter.next() {
        let mut value = || iter.next().cloned().ok_or_else(|| format!("{arg} needs a value"));
        match arg.as_str() {
            "--protoc" => protoc = value()?,
            "--proto-root" => proto_root = Some(PathBuf::from(value()?)),
            "--out" => out = Some(PathBuf::from(value()?)),
            "--check" => check = true,
            _ => return Err(format!("unknown argument {arg}")),
        }
    }
    Ok(Options {
        protoc,
        proto_root: proto_root.ok_or("--proto-root is required")?,
        out: out.ok_or("--out is required")?,
        check,
    })
}

fn sorted_file_names(dir: &Path, extension: &str) -> Result<Vec<String>, String> {
    let entries = std::fs::read_dir(dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    let mut names: Vec<String> = entries.filter_map(Result::ok)
        .map(|entry| entry.file_name().to_string_lossy().into_owned())
        .filter(|name| name.ends_with(extension))
        .collect();
    names.sort();
    Ok(names)
}

fn run_standalone(args: &[String]) -> Result<(), String> {
    let options = parse_options(args)?;
    let package_dir = options.proto_root.join(model::PACKAGE.replace('.', "/"));
    let names: Vec<String> = sorted_file_names(&package_dir, ".proto")?.into_iter()
        .map(|name| format!("{}/{name}", model::PACKAGE.replace('.', "/")))
        .collect();
    let descriptor_set = std::env::temp_dir().join(format!("reboot-api-{}.pb", std::process::id()));
    let status = Command::new(&options.protoc)
        .arg(format!("-I{}", options.proto_root.display()))
        .args(["--include_imports", "--include_source_info"])
        .arg(format!("--descriptor_set_out={}", descriptor_set.display()))
        .args(&names)
        .status()
        .map_err(|e| format!("running {}: {e}", options.protoc))?;
    let bytes = std::fs::read(&descriptor_set);
    let _ = std::fs::remove_file(&descriptor_set);
    if !status.success() {
        return Err(format!("{} failed", options.protoc));
    }
    let bytes = bytes.map_err(|e| format!("reading the descriptor set: {e}"))?;
    let mut pool = DescriptorPool::new();
    pool.decode_file_descriptor_set(bytes.as_slice()).map_err(|e| e.to_string())?;
    let outputs = generate(&pool, &names)?;

    let mut stale = Vec::new();
    for (name, content) in &outputs {
        let path = options.out.join(name);
        if std::fs::read_to_string(&path).is_ok_and(|current| &current == content) {
            continue;
        }
        stale.push(name.clone());
        if !options.check {
            if let Some(parent) = path.parent() {
                std::fs::create_dir_all(parent).map_err(|e| format!("{}: {e}", parent.display()))?;
            }
            std::fs::write(&path, content).map_err(|e| format!("{}: {e}", path.display()))?;
        }
    }
    let header_dir = options.out.join(cpp::API_DIR).join(cpp::HEADER_DIR);
    for name in sorted_file_names(&header_dir, ".hpp")? {
        let relative = format!("{}/{}/{name}", cpp::API_DIR, cpp::HEADER_DIR);
        if outputs.contains_key(&relative) {
            continue;
        }
        stale.push(relative);
        if !options.check {
            let path = header_dir.join(&name);
            std::fs::remove_file(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        }
    }
    if options.check && !stale.is_empty() {
        return Err(format!("generated C++ is out of date: {}", stale.join(", ")));
    }
    Ok(())
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let result = if args.is_empty() { run_plugin() } else { run_standalone(&args) };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
