import 'dart:ui';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/server.dart';

import 'package:reboot_launcher/src/util/reboot.dart';

class SettingsController extends GetxController {
  static const String _kDefaultIp = "127.0.0.1";
  static const bool _kDefaultAutoUpdate = true;

  late final GetStorage _storage;
  late final String originalDll;
  late final TextEditingController updateUrl;
  late final TextEditingController rebootDll;
  late final TextEditingController consoleDll;
  late final TextEditingController authDll;
  late final TextEditingController matchmakingIp;
  late final RxBool autoUpdate;
  late final RxBool firstRun;
  late final RxInt index;
  late double width;
  late double height;
  late double? offsetX;
  late double? offsetY;
  late double scrollingDistance;

  SettingsController() {
    _storage = GetStorage("reboot_settings");
    updateUrl = TextEditingController(text: _storage.read("update_url") ?? rebootDownloadUrl);
    updateUrl.addListener(() => _storage.write("update_url", updateUrl.text));
    rebootDll = _createController("reboot", "reboot.dll");
    consoleDll = _createController("console", "console.dll");
    authDll = _createController("cobalt", "cobalt.dll");
    matchmakingIp = TextEditingController(text: _storage.read("ip") ?? _kDefaultIp);
    matchmakingIp.addListener(() async {
      var text = matchmakingIp.text;
      _storage.write("ip", text);
      writeMatchmakingIp(text);
    });
    width = _storage.read("width") ?? kDefaultWindowWidth;
    height = _storage.read("height") ?? kDefaultWindowHeight;
    offsetX = _storage.read("offset_x");
    offsetY = _storage.read("offset_y");
    autoUpdate = RxBool(_storage.read("auto_update") ?? _kDefaultAutoUpdate);
    autoUpdate.listen((value) => _storage.write("auto_update", value));
    scrollingDistance = 0.0;
    firstRun = RxBool(_storage.read("first_run") ?? true);
    firstRun.listen((value) => _storage.write("first_run", value));
    index = RxInt(firstRun() ? 3 : 0);
  }

  TextEditingController _createController(String key, String name) {
    var controller = TextEditingController(text: _storage.read(key) ?? _controllerDefaultPath(name));
    controller.addListener(() => _storage.write(key, controller.text));
    return controller;
  }

  void saveWindowSize() {
    _storage.write("width", window.physicalSize.width);
    _storage.write("height", window.physicalSize.height);
  }

  void saveWindowOffset(Offset position) {
    _storage.write("offset_x", position.dx);
    _storage.write("offset_y", position.dy);
  }

  void reset(){
    updateUrl.text = rebootDownloadUrl;
    rebootDll.text = _controllerDefaultPath("reboot.dll");
    consoleDll.text = _controllerDefaultPath("console.dll");
    authDll.text = _controllerDefaultPath("cobalt.dll");
    matchmakingIp.text = _kDefaultIp;
    writeMatchmakingIp(_kDefaultIp);
    autoUpdate.value = _kDefaultAutoUpdate;
  }

  String _controllerDefaultPath(String name) => "${assetsDirectory.path}\\dlls\\$name";
}
