import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_common/common.dart';
import 'package:window_manager/window_manager.dart';

class SettingsController extends GetxController {
  static const String _kDefaultIp = "127.0.0.1";

  late final GetStorage _storage;
  late final String originalDll;
  late final TextEditingController rebootDll;
  late final TextEditingController consoleDll;
  late final TextEditingController authDll;
  late final RxBool firstRun;
  late double width;
  late double height;
  late double? offsetX;
  late double? offsetY;
  late double scrollingDistance;

  SettingsController() {
    _storage = GetStorage("reboot_settings");
    rebootDll = _createController("reboot", "reboot.dll");
    consoleDll = _createController("console", "console.dll");
    authDll = _createController("cobalt", "cobalt.dll");
    width = _storage.read("width") ?? kDefaultWindowWidth;
    height = _storage.read("height") ?? kDefaultWindowHeight;
    offsetX = _storage.read("offset_x");
    offsetY = _storage.read("offset_y");
    scrollingDistance = 0.0;
    firstRun = RxBool(_storage.read("first_run") ?? true);
    firstRun.listen((value) => _storage.write("first_run", value));
  }

  TextEditingController _createController(String key, String name) {
    var controller = TextEditingController(text: _storage.read(key) ?? _controllerDefaultPath(name));
    controller.addListener(() => _storage.write(key, controller.text));
    return controller;
  }

  void saveWindowSize() async {
    var size = await windowManager.getSize();
    _storage.write("width", size.width);
    _storage.write("height", size.height);
  }

  void saveWindowOffset(Offset position) {
    _storage.write("offset_x", position.dx);
    _storage.write("offset_y", position.dy);
  }

  void reset(){
    rebootDll.text = _controllerDefaultPath("reboot.dll");
    consoleDll.text = _controllerDefaultPath("console.dll");
    authDll.text = _controllerDefaultPath("cobalt.dll");
    firstRun.value = true;
    writeMatchmakingIp(_kDefaultIp);
  }

  String _controllerDefaultPath(String name) => "${assetsDirectory.path}\\dlls\\$name";
}
