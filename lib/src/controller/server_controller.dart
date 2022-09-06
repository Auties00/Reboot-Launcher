import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';

import 'package:reboot_launcher/src/util/binary.dart';

class ServerController extends GetxController {
  late final TextEditingController host;
  late final TextEditingController port;
  late final RxBool embedded;
  late final RxBool started;
  Process? process;

  ServerController() {
    var storage = GetStorage("server");
    host = TextEditingController(text: storage.read("host") ?? "");
    host.addListener(() => storage.write("host", host.text));

    port = TextEditingController(text: storage.read("port") ?? "");
    port.addListener(() => storage.write("port", port.text));

    embedded = RxBool(storage.read("embedded") ?? true);
    embedded.listen((value) => storage.write("embedded", value));

    started = RxBool(false);
  }

  Future kill() async {
    var release = await loadBinary("release.bat", false);
    return Process.run(release.path, []);
  }
}