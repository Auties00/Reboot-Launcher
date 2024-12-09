import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:http/http.dart' as http;
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/messenger/abstract/info_bar.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:version/version.dart';
import 'package:yaml/yaml.dart';

class SettingsController extends GetxController {
  static const String storageName = "v2_settings_storage";

  late final GetStorage? _storage;
  late final RxString language;
  late final Rx<ThemeMode> themeMode;
  late final RxBool firstRun;
  late double width;
  late double height;
  late double? offsetX;
  late double? offsetY;

  SettingsController() {
    _storage = appWithNoStorage ? null : GetStorage(storageName);
    width = _storage?.read("width") ?? kDefaultWindowWidth;
    height = _storage?.read("height") ?? kDefaultWindowHeight;
    offsetX = _storage?.read("offset_x");
    offsetY = _storage?.read("offset_y");
    themeMode = Rx(ThemeMode.values.elementAt(_storage?.read("theme") ?? 0));
    themeMode.listen((value) => _storage?.write("theme", value.index));
    language = RxString(_storage?.read("language") ?? currentLocale);
    language.listen((value) => _storage?.write("language", value));
    firstRun = RxBool(_storage?.read("first_run_tutorial") ?? true);
    firstRun.listen((value) => _storage?.write("first_run_tutorial", value));
  }

  void saveWindowSize(Size size) {
    _storage?.write("width", size.width);
    _storage?.write("height", size.height);
  }

  void saveWindowOffset(Offset position) {
    offsetX = position.dx;
    offsetY = position.dy;
    _storage?.write("offset_x", offsetX);
    _storage?.write("offset_y", offsetY);
  }

  Future<void> notifyLauncherUpdate() async {
    if (appVersion == null) {
      return;
    }

    final pubspec = await _getPubspecYaml();
    if (pubspec == null) {
      return;
    }

    final latestVersion = Version.parse(pubspec["version"]);
    if (latestVersion <= appVersion) {
      return;
    }

    late InfoBarEntry infoBar;
    infoBar = showRebootInfoBar(
        translations.updateAvailable(latestVersion.toString()),
        duration: null,
        severity: InfoBarSeverity.warning,
        action: Button(
          child: Text(translations.updateAvailableAction),
          onPressed: () {
            infoBar.close();
            launchUrl(Uri.parse(
                "https://github.com/Auties00/reboot_launcher/releases"));
          },
        )
    );
  }

  Future<dynamic> _getPubspecYaml() async {
    try {
      final pubspecResponse = await http.get(Uri.parse(
          "https://raw.githubusercontent.com/Auties00/reboot_launcher/master/gui/pubspec.yaml"));
      if (pubspecResponse.statusCode != 200) {
        return null;
      }

      return loadYaml(pubspecResponse.body);
    } catch (error) {
      log("[UPDATER] Cannot check for updates: $error");
      return null;
    }
  }
}