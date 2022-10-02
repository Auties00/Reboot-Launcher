import 'dart:convert';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';

class GameController extends GetxController {
  late final GetStorage _storage;
  late final TextEditingController username;
  late final TextEditingController version;
  late final Rx<List<FortniteVersion>> versions;
  late final Rxn<FortniteVersion> _selectedVersion;
  late final RxBool host;
  late final RxBool started;
  Process? gameProcess;
  Process? launcherProcess;
  Process? eacProcess;

  GameController() {
    _storage = GetStorage("game");

    Iterable decodedVersionsJson =
        jsonDecode(_storage.read("versions") ?? "[]");
    var decodedVersions = decodedVersionsJson
        .map((entry) => FortniteVersion.fromJson(entry))
        .toList();
    versions = Rx(decodedVersions);
    versions.listen((data) => saveVersions());

    var decodedSelectedVersionName = _storage.read("version");
    var decodedSelectedVersion = decodedVersions.firstWhereOrNull(
        (element) => element.name == decodedSelectedVersionName);
    _selectedVersion = Rxn(decodedSelectedVersion);

    host = RxBool(_storage.read("host") ?? false);
    host.listen((value) {
      _storage.write("host", value);
      username.text = _storage.read("${host.value ? 'host' : 'game'}_username") ?? "";
    });

    username = TextEditingController(text: _storage.read("${host.value ? 'host' : 'game'}_username") ?? "");
    username.addListener(() async {
      await _storage.write("${host.value ? 'host' : 'game'}_username", username.text);
    });

    started = RxBool(false);
  }

  void kill() {
    gameProcess?.kill(ProcessSignal.sigabrt);
    launcherProcess?.kill(ProcessSignal.sigabrt);
    eacProcess?.kill(ProcessSignal.sigabrt);
  }

  FortniteVersion? getVersionByName(String name) {
    return versions.value.firstWhereOrNull((element) => element.name == name);
  }

  void addVersion(FortniteVersion version) {
    versions.update((val) => val?.add(version));
  }

  FortniteVersion removeVersionByName(String versionName) {
    var version =
        versions.value.firstWhere((element) => element.name == versionName);
    removeVersion(version);
    return version;
  }

  void removeVersion(FortniteVersion version) {
    versions.update((val) => val?.remove(version));
  }

  Future saveVersions() async {
    var serialized =
        jsonEncode(versions.value.map((entry) => entry.toJson()).toList());
    await _storage.write("versions", serialized);
  }

  bool get hasVersions => versions.value.isNotEmpty;

  bool get hasNoVersions => versions.value.isEmpty;

  Rxn<FortniteVersion> get selectedVersionObs => _selectedVersion;

  set selectedVersion(FortniteVersion? version) {
    _selectedVersion(version);
    _storage.write("version", version?.name);
  }
}
