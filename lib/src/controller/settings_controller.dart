import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/model/tutorial_page.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'dart:ui';

import '../util/reboot.dart';

class SettingsController extends GetxController {
  late final GetStorage _storage;
  late final String originalDll;
  late final TextEditingController updateUrl;
  late final TextEditingController rebootDll;
  late final TextEditingController consoleDll;
  late final TextEditingController authDll;
  late final TextEditingController matchmakingIp;
  late final Rx<PaneDisplayMode> displayType;
  late final RxBool automaticallyStartMatchmaker;
  late final RxBool doNotAskAgain;
  late final RxBool advancedMode;
  late final RxBool autoUpdate;
  late Rx<TutorialPage> tutorialPage;
  late double width;
  late double height;
  late double? offsetX;
  late double? offsetY;
  late double scrollingDistance;

  SettingsController() {
    _storage = GetStorage("settings");

    updateUrl = TextEditingController(text: _storage.read("update_url") ?? rebootDownloadUrl);
    updateUrl.addListener(() => _storage.write("update_url", updateUrl.text));

    rebootDll = _createController("reboot", "reboot.dll");

    consoleDll = _createController("console", "console.dll");

    authDll = _createController("cranium2", "craniumv2.dll");

    matchmakingIp = TextEditingController(text: _storage.read("ip") ?? "127.0.0.1");
    matchmakingIp.addListener(() async {
      var text = matchmakingIp.text;
      _storage.write("ip", text);
    });

    automaticallyStartMatchmaker = RxBool(_storage.read("start_matchmaker_automatically") ?? false);
    automaticallyStartMatchmaker.listen((value) => _storage.write("start_matchmaker_automatically", value));

    doNotAskAgain = RxBool(_storage.read("do_not_ask_again") ?? false);
    doNotAskAgain.listen((value) => _storage.write("do_not_ask_again", value));

    width = _storage.read("width") ?? window.physicalSize.width;
    height = _storage.read("height") ?? window.physicalSize.height;
    offsetX = _storage.read("offset_x");
    offsetY = _storage.read("offset_y");

    advancedMode = RxBool(_storage.read("advanced") ?? false);
    advancedMode.listen((value) async => _storage.write("advanced", value));
    autoUpdate = RxBool(_storage.read("auto_update") ?? false);
    autoUpdate.listen((value) async => _storage.write("auto_update", value));

    displayType = Rx(PaneDisplayMode.top);

    scrollingDistance = 0.0;

    tutorialPage = Rx(TutorialPage.start);
  }

  TextEditingController _createController(String key, String name) {
    loadBinary(name, true);

    var controller = TextEditingController(text: _storage.read(key) ?? "${safeBinariesDirectory.path}\\$name");
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
}
