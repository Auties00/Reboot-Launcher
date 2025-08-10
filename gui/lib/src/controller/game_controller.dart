import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:version/version.dart';

class GameController extends GetxController {
  static const String storageName = "v3_game_storage";

  late final GetStorage? _storage;
  late final TextEditingController username;
  late final TextEditingController password;
  late final TextEditingController customLaunchArgs;
  late final Rx<List<GameVersion>> versions;
  late final Rxn<GameVersion> selectedVersion;
  late final RxBool started;
  late final Rxn<GameInstance> instance;
  
  GameController() {
    _storage = appWithNoStorage ? null : GetStorage(storageName);
    Iterable decodedVersionsJson = jsonDecode(_storage?.read("versions") ?? "[]");
    final decodedVersions = decodedVersionsJson
        .map((entry) => GameVersion.fromJson(entry))
        .toList();
    versions = Rx(decodedVersions);
    versions.listen((data) => _saveVersions());
    final decodedSelectedVersionName = _storage?.read("version");
    selectedVersion = Rxn(decodedVersions.firstWhereOrNull((element) => element.name == decodedSelectedVersionName));
    selectedVersion.listen((version) => _storage?.write("version", version?.name));
    username = TextEditingController(
        text: _storage?.read("username") ?? kDefaultPlayerName);
    username.addListener(() => _storage?.write("username", username.text));
    password = TextEditingController(text: _storage?.read("password") ?? "");
    password.addListener(() => _storage?.write("password", password.text));
    customLaunchArgs = TextEditingController(text: _storage?.read("custom_launch_args") ?? "");
    customLaunchArgs.addListener(() => _storage?.write("custom_launch_args", customLaunchArgs.text));
    started = RxBool(false);
    instance = Rxn();
  }

  void reset() {
    username.text = kDefaultPlayerName;
    password.text = "";
    customLaunchArgs.text = "";
    versions.value = [];
    selectedVersion.value = null;
    instance.value = null;
  }

  GameVersion? getVersionByName(String name) {
    name = name.trim();
    return versions.value.firstWhereOrNull((element) => element.name == name);
  }

  GameVersion? getVersionByGame(String gameVersion) {
    gameVersion = gameVersion.trim();
    final parsedGameVersion = Version.parse(gameVersion);
    return versions.value.firstWhereOrNull((element) {
      final compare = element.gameVersion.trim();
      try {
        final parsedCompare = Version.parse(compare);
        return parsedCompare.major == parsedGameVersion.major
            && parsedCompare.minor == parsedGameVersion.minor;
      } on FormatException {
        return compare == gameVersion;
      }
    });
  }

  void addVersion(GameVersion version) {
    versions.update((val) => val?.add(version));
    selectedVersion.value = version;
  }

  void removeVersion(GameVersion version) {
    final index = versions.value.indexOf(version);
    versions.update((val) => val?.removeAt(index));
    if(hasNoVersions) {
      selectedVersion.value = null;
    }else {
      selectedVersion.value = versions.value.elementAt(max(0, index - 1));
    }
  }

  Future<void> _saveVersions() async {
    var serialized = jsonEncode(versions.value.map((entry) => entry.toJson()).toList());
    await _storage?.write("versions", serialized);
  }

  bool get hasVersions => versions.value.isNotEmpty;

  bool get hasNoVersions => versions.value.isEmpty;

  void updateVersion(GameVersion version, Function(GameVersion) function) => versions.update((val) => function(version));
}
