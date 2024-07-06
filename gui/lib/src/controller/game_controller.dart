import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/util/keyboard.dart';

import '../../main.dart';

class GameController extends GetxController {
  static const PhysicalKeyboardKey _kDefaultConsoleKey = PhysicalKeyboardKey(0x00070041);

  late final GetStorage? _storage;
  late final TextEditingController username;
  late final TextEditingController password;
  late final TextEditingController customLaunchArgs;
  late final Rx<List<FortniteVersion>> versions;
  late final Rxn<FortniteVersion> _selectedVersion;
  late final RxBool started;
  late final Rxn<GameInstance> instance;
  late final Rx<PhysicalKeyboardKey> consoleKey;
  
  GameController() {
    _storage = appWithNoStorage ? null : GetStorage("game_storage");
    Iterable decodedVersionsJson = jsonDecode(_storage?.read("versions") ?? "[]");
    final decodedVersions = decodedVersionsJson
        .map((entry) => FortniteVersion.fromJson(entry))
        .toList();
    versions = Rx(decodedVersions);
    versions.listen((data) => _saveVersions());
    final decodedSelectedVersionName = _storage?.read("version");
    final decodedSelectedVersion = decodedVersions.firstWhereOrNull((element) => element.content.toString() == decodedSelectedVersionName);
    _selectedVersion = Rxn(decodedSelectedVersion);
    username = TextEditingController(
        text: _storage?.read("username") ?? kDefaultPlayerName);
    username.addListener(() => _storage?.write("username", username.text));
    password = TextEditingController(text: _storage?.read("password") ?? "");
    password.addListener(() => _storage?.write("password", password.text));
    customLaunchArgs = TextEditingController(text: _storage?.read("custom_launch_args") ?? "");
    customLaunchArgs.addListener(() =>
        _storage?.write("custom_launch_args", customLaunchArgs.text));
    started = RxBool(false);
    instance = Rxn();
    consoleKey = Rx(_readConsoleKey());
    _writeConsoleKey(consoleKey.value);
    consoleKey.listen((newValue) {
      _storage?.write("console_key", newValue.usbHidUsage);
      _writeConsoleKey(newValue);
    });
  }

  PhysicalKeyboardKey _readConsoleKey() {
    final consoleKeyValue = _storage?.read("console_key");
    if(consoleKeyValue == null) {
      return _kDefaultConsoleKey;
    }

    final consoleKeyNumber = int.tryParse(consoleKeyValue.toString());
    if(consoleKeyNumber == null) {
      return _kDefaultConsoleKey;
    }

    final consoleKey = PhysicalKeyboardKey(consoleKeyNumber);
    if(!consoleKey.isUnrealEngineKey) {
      return _kDefaultConsoleKey;
    }

    return consoleKey;
  }

  Future<void> _writeConsoleKey(PhysicalKeyboardKey keyValue) async {
    final defaultInput = File("${backendDirectory.path}\\CloudStorage\\DefaultInput.ini");
    await defaultInput.parent.create(recursive: true);
    await defaultInput.writeAsString("[/Script/Engine.InputSettings]\n+ConsoleKeys=Tilde\n+ConsoleKeys=${keyValue.unrealEngineName}", flush: true);
  }

  void reset() {
    username.text = kDefaultPlayerName;
    password.text = "";
    customLaunchArgs.text = "";
    versions.value = [];
    instance.value = null;
  }

  FortniteVersion? getVersionByName(String name) {
    return versions.value.firstWhereOrNull((element) => element.content.toString() == name);
  }

  void addVersion(FortniteVersion version) {
    var empty = versions.value.isEmpty;
    versions.update((val) => val?.add(version));
    if(empty){
      selectedVersion = version;
    }
  }

  void removeVersion(FortniteVersion version) {
    versions.update((val) => val?.remove(version));
    if (selectedVersion == version || hasNoVersions) {
      selectedVersion = null;
    }
  }

  Future<void> _saveVersions() async {
    var serialized = jsonEncode(versions.value.map((entry) => entry.toJson()).toList());
    await _storage?.write("versions", serialized);
  }

  bool get hasVersions => versions.value.isNotEmpty;

  bool get hasNoVersions => versions.value.isEmpty;

  FortniteVersion? get selectedVersion => _selectedVersion();

  set selectedVersion(FortniteVersion? version) {
    _selectedVersion.value = version;
    _storage?.write("version", version?.content.toString());
  }

  void updateVersion(FortniteVersion version, Function(FortniteVersion) function) {
    versions.update((val) => function(version));
  }
}
