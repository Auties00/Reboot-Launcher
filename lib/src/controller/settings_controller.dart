import 'dart:convert';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:system_theme/system_theme.dart';

class SettingsController extends GetxController {
  late final GetStorage _storage;
  late final String originalDll;
  late final TextEditingController rebootDll;
  late final TextEditingController consoleDll;
  late final TextEditingController craniumDll;
  late final RxBool autoUpdate;

  SettingsController() {
    _storage = GetStorage("settings");

    rebootDll = _createController("reboot", "reboot.dll");
    consoleDll = _createController("console", "console.dll");
    craniumDll = _createController("cranium", "cranium.dll");
    autoUpdate = RxBool(_storage.read("auto_update") ?? true);

    autoUpdate.listen((value) => _storage.write("auto_update", value));
  }

  TextEditingController _createController(String key, String name) {
    loadBinary(name, true);

    var controller = TextEditingController(text: _storage.read(key) ?? "$safeBinariesDirectory\\$name");
    controller.addListener(() => _storage.write(key, controller.text));

    return controller;
  }
}
