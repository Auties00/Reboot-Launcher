import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:http/http.dart' as http;
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/messenger/info_bar.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:version/version.dart';
import 'package:yaml/yaml.dart';

class SettingsController extends GetxController {
  static const String storageName = "v3_settings_storage";

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
}