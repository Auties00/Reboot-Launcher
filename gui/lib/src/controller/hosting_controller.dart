import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
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
  late final RxBool headless;
  late final RxBool started;
  late final RxBool published;
  late final RxBool automaticServer;
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
    headless = RxBool(_storage.read("headless") ?? true);
    headless.listen((value) => _storage.write("headless", value));
    started = RxBool(false);
    published = RxBool(false);
    showPassword = RxBool(false);
    instance = Rxn();
    automaticServer = RxBool(_storage.read("auto") ?? true);
    automaticServer.listen((value) => _storage.write("auto", value));
    final supabase = Supabase.instance.client;
    servers = Rxn();
    supabase.from("hosting")
        .stream(primaryKey: ['id'])
        .map((event) => _parseValidServers(event))
        .listen((event) {
          servers.value = event;
          published.value = event.any((element) => element["id"] == uuid);
        });
  }

  Set<Map<String, dynamic>> _parseValidServers(event) => event.where((element) => element["ip"] != null).toSet();

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
