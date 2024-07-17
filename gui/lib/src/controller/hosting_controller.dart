import 'dart:convert';

import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/util/cryptography.dart';
import 'package:supabase_flutter/supabase_flutter.dart';
import 'package:sync/semaphore.dart';
import 'package:uuid/uuid.dart';

class HostingController extends GetxController {
  late final GetStorage? _storage;
  late final String uuid;
  late final TextEditingController name;
  late final FocusNode nameFocusNode;
  late final TextEditingController description;
  late final FocusNode descriptionFocusNode;
  late final TextEditingController password;
  late final FocusNode passwordFocusNode;
  late final RxBool showPassword;
  late final RxBool discoverable;
  late final Rx<GameServerType> type;
  late final RxBool autoRestart;
  late final RxBool started;
  late final RxBool published;
  late final Rxn<GameInstance> instance;
  late final Rxn<Set<FortniteServer>> servers;
  late final Semaphore _semaphore;

  HostingController() {
    _storage = appWithNoStorage ? null : GetStorage("hosting_storage");
    uuid = _storage?.read("uuid") ?? const Uuid().v4();
    _storage?.write("uuid", uuid);
    name = TextEditingController(text: _storage?.read("name"));
    name.addListener(() => _storage?.write("name", name.text));
    description = TextEditingController(text: _storage?.read("description"));
    description.addListener(() => _storage?.write("description", description.text));
    password = TextEditingController(text: _storage?.read("password") ?? "");
    password.addListener(() => _storage?.write("password", password.text));
    nameFocusNode = FocusNode();
    descriptionFocusNode = FocusNode();
    passwordFocusNode = FocusNode();
    discoverable = RxBool(_storage?.read("discoverable") ?? false);
    discoverable.listen((value) => _storage?.write("discoverable", value));
    type = Rx(GameServerType.values.elementAt(_storage?.read("type") ?? GameServerType.headless.index));
    type.listen((value) => _storage?.write("type", value.index));
    autoRestart = RxBool(_storage?.read("auto_restart") ?? true);
    autoRestart.listen((value) => _storage?.write("auto_restart", value));
    started = RxBool(false);
    published = RxBool(false);
    showPassword = RxBool(false);
    instance = Rxn();
    final supabase = Supabase.instance.client;
    servers = Rxn();
    supabase.from("hosting_v2")
        .stream(primaryKey: ['id'])
        .map((event) => event.map((element) => FortniteServer.fromJson(element)).where((element) => element.ip.isNotEmpty).toSet())
        .listen((event) {
          servers.value = event;
          published.value = event.any((element) => element.id == uuid);
        });
    _semaphore = Semaphore();
  }

  Future<void> publishServer(String author, String version) async {
    try {
      _semaphore.acquire();
      log("[SERVER] Publishing server...");
      if(published.value) {
        log("[SERVER] Already published");
        return;
      }

      final passwordText = password.text;
      final hasPassword = passwordText.isNotEmpty;
      var ip = await Ipify.ipv4();
      if(hasPassword) {
        ip = aes256Encrypt(ip, passwordText);
      }

      final supabase = Supabase.instance.client;
      final hosts = supabase.from("hosting_v2");
      final payload = FortniteServer(
          id: uuid,
          name: name.text,
          description: description.text,
          author: author,
          ip: ip,
          version: version,
          password: hasPassword ? hashPassword(passwordText) : null,
          timestamp: DateTime.now(),
          discoverable: discoverable.value
      ).toJson();
      log("[SERVER] Payload: ${jsonEncode(payload)}");
      if(published()) {
        await hosts.update(payload)
            .eq("id", uuid);
      }else {
        await hosts.insert(payload);
      }

      published.value = true;
      log("[SERVER] Published");
    }catch(error) {
      log("[SERVER] Cannot publish server: $error");
      published.value = false;
    }finally {
      _semaphore.release();
    }
  }

  Future<void> discardServer() async {
    try {
      _semaphore.acquire();
      log("[SERVER] Discarding server...");
      final supabase = Supabase.instance.client;
      await supabase.from("hosting_v2")
          .delete()
          .match({'id': uuid});
      servers.value?.removeWhere((element) => element.id == uuid);
      log("[SERVER] Discarded server");
    }catch(error) {
      log("[SERVER] Cannot discard server: $error");
    }finally {
      published.value = false;
      _semaphore.release();
    }
  }

  void reset() {
    name.text = "";
    description.text = "";
    showPassword.value = false;
    discoverable.value = false;
    started.value = false;
    instance.value = null;
    type.value = GameServerType.headless;
    autoRestart.value = true;
  }

  FortniteServer? findServerById(String uuid) {
    try {
      return servers.value?.firstWhere((element) => element.id == uuid);
    } on StateError catch(_) {
      return null;
    }
  }
}
