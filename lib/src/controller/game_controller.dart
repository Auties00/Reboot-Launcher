import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:io';

import 'package:async/async.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/model/game_instance.dart';
import 'package:reboot_launcher/src/model/game_type.dart';

class GameController extends GetxController {
  late final GetStorage _storage;
  late final TextEditingController username;
  late final TextEditingController version;
  late final Rx<List<FortniteVersion>> versions;
  late final Rxn<FortniteVersion> _selectedVersion;
  late final Rx<GameType> type;
  late final HashMap<GameType, GameInstance> gameInstancesMap;
  late final RxBool started;
  late bool updated;
  late bool error;
  late bool failing;
  StreamController<bool>? updater;

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

    type = Rx(GameType.values.elementAt(_storage.read("type") ?? 0));
    type.listen((value) {
      _storage.write("type", value.index);
      username.text = _readUsername();
    });

    username = TextEditingController(text: _readUsername());
    username.addListener(() => _storage.write("${type.value == GameType.client ? 'game' : 'host'}_username", username.text));

    gameInstancesMap= HashMap();

    started = RxBool(false);

    updated = false;

    error = false;

    failing = false;
  }

  String _readUsername() {
    var client = type.value == GameType.client;
    return _storage.read("${client ? 'game' : 'host'}_username") ?? (client ? "" : "HostingServer");
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

  GameInstance? get currentGameInstance => gameInstancesMap[type()];

  set selectedVersion(FortniteVersion? version) {
    _selectedVersion(version);
    _storage.write("version", version?.name);
  }

  void updateVersion(FortniteVersion version, Function(FortniteVersion) function) {
    versions.update((val) => function(version));
  }
}
