import 'dart:async';
import 'dart:convert';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';

class GameController extends GetxController {
  late final GetStorage _storage;
  late final TextEditingController username;
  late final TextEditingController password;
  late final TextEditingController customLaunchArgs;
  late final Rx<List<FortniteVersion>> versions;
  late final Rxn<FortniteVersion> _selectedVersion;
  late final RxBool started;
  late final Rxn<GameInstance> instance;
  
  GameController() {
    _storage = GetStorage("game");
    Iterable decodedVersionsJson = jsonDecode(
        _storage.read("versions") ?? "[]");
    var decodedVersions = decodedVersionsJson
        .map((entry) => FortniteVersion.fromJson(entry))
        .toList();
    versions = Rx(decodedVersions);
    versions.listen((data) => _saveVersions());
    var decodedSelectedVersionName = _storage.read("version");
    var decodedSelectedVersion = decodedVersions.firstWhereOrNull((
        element) => element.name == decodedSelectedVersionName);
    _selectedVersion = Rxn(decodedSelectedVersion);
    username = TextEditingController(
        text: _storage.read("username") ?? kDefaultPlayerName);
    username.addListener(() => _storage.write("username", username.text));
    password = TextEditingController(text: _storage.read("password") ?? "");
    password.addListener(() => _storage.write("password", password.text));
    customLaunchArgs =
        TextEditingController(text: _storage.read("custom_launch_args") ?? "");
    customLaunchArgs.addListener(() =>
        _storage.write("custom_launch_args", customLaunchArgs.text));
    started = RxBool(false);
    instance = Rxn();
  }

  void reset() {
    username.text = kDefaultPlayerName;
    password.text = "";
    customLaunchArgs.text = "";
    versions.value = [];
    instance.value = null;
  }

  FortniteVersion? getVersionByName(String name) {
    return versions.value.firstWhereOrNull((element) => element.name == name);
  }

  void addVersion(FortniteVersion version) {
    var empty = versions.value.isEmpty;
    versions.update((val) => val?.add(version));
    if(empty){
      selectedVersion = version;
    }
  }

  FortniteVersion removeVersionByName(String versionName) {
    var version = versions.value.firstWhere((element) => element.name == versionName);
    removeVersion(version);
    return version;
  }

  void removeVersion(FortniteVersion version) {
    versions.update((val) => val?.remove(version));
    if (selectedVersion?.name == version.name || hasNoVersions) {
      selectedVersion = null;
    }
  }

  Future<void> _saveVersions() async {
    var serialized = jsonEncode(versions.value.map((entry) => entry.toJson()).toList());
    await _storage.write("versions", serialized);
  }

  bool get hasVersions => versions.value.isNotEmpty;

  bool get hasNoVersions => versions.value.isEmpty;

  FortniteVersion? get selectedVersion => _selectedVersion();

  set selectedVersion(FortniteVersion? version) {
    _selectedVersion.value = version;
    _storage.write("version", version?.name);
  }

  void updateVersion(FortniteVersion version, Function(FortniteVersion) function) {
    versions.update((val) => function(version));
  }
}
