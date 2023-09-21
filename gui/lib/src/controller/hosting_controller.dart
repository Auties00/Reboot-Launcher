import 'dart:convert';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:supabase_flutter/supabase_flutter.dart';
import 'package:uuid/uuid.dart';

class HostingController extends GetxController {
  late final GetStorage _storage;
  late final String uuid;
  late final TextEditingController name;
  late final TextEditingController description;
  late final TextEditingController password;
  late final RxBool showPassword;
  late final RxBool discoverable;
  late final RxBool started;
  late final RxBool published;
  late final Rxn<GameInstance> instance;
  late final Rxn<Set<Map<String, dynamic>>> servers;

  HostingController() {
    _storage = GetStorage("hosting");
    uuid = _storage.read("uuid") ?? const Uuid().v4();
    _storage.write("uuid", uuid);
    name = TextEditingController(text: _storage.read("name"));
    name.addListener(() => _storage.write("name", name.text));
    description = TextEditingController(text: _storage.read("description"));
    description.addListener(() => _storage.write("description", description.text));
    password = TextEditingController(text: _storage.read("password") ?? "");
    password.addListener(() => _storage.write("password", password.text));
    discoverable = RxBool(_storage.read("discoverable") ?? true);
    discoverable.listen((value) => _storage.write("discoverable", value));
    started = RxBool(false);
    published = RxBool(false);
    showPassword = RxBool(false);
    var serializedInstance = _storage.read("instance");
    instance = Rxn(serializedInstance != null ? GameInstance.fromJson(jsonDecode(serializedInstance)) : null);
    instance.listen((_) => saveInstance());
    var supabase = Supabase.instance.client;
    servers = Rxn();
    supabase.from('hosts')
        .stream(primaryKey: ['id'])
        .map((event) => _parseValidServers(event))
        .listen((event) => servers.value = event);
  }

  Set<Map<String, dynamic>> _parseValidServers(event) => event.where((element) => element["ip"] != null).toSet();

  Future<void> saveInstance() => _storage.write("instance", jsonEncode(instance.value?.toJson()));

  void reset() {
    name.text = "";
    description.text = "";
    showPassword.value = false;
    discoverable.value = false;
    started.value = false;
    instance.value = null;
  }

  Map<String, dynamic>? findServerById(String uuid) {
    try {
      return servers.value?.firstWhere((element) => element["id"] == uuid);
    } on StateError catch(_) {
      return null;
    }
  }
}
