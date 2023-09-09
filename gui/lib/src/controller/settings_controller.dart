import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_common/common.dart';

class SettingsController extends GetxController {
  late final GetStorage _storage;
  late final String originalDll;
  late final TextEditingController gameServerDll;
  late final TextEditingController unrealEngineConsoleDll;
  late final TextEditingController authenticatorDll;
  late final TextEditingController gameServerPort;
  late final RxBool firstRun;
  late double width;
  late double height;
  late double? offsetX;
  late double? offsetY;
  late double scrollingDistance;

  SettingsController() {
    _storage = GetStorage("reboot_settings");
    gameServerDll = _createController("game_server", "reboot.dll");
    unrealEngineConsoleDll = _createController("unreal_engine_console", "console.dll");
    authenticatorDll = _createController("authenticator", "cobalt.dll");
    gameServerPort = TextEditingController(text: _storage.read("game_server_port") ?? kDefaultGameServerPort);
    gameServerPort.addListener(() => _storage.write("game_server_port", gameServerPort.text));
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

  void saveWindowSize(Size size) {
    _storage.write("width", size.width);
    _storage.write("height", size.height);
  }

  void saveWindowOffset(Offset position) {
    _storage.write("offset_x", position.dx);
    _storage.write("offset_y", position.dy);
  }

  void reset(){
    gameServerDll.text = _controllerDefaultPath("reboot.dll");
    unrealEngineConsoleDll.text = _controllerDefaultPath("console.dll");
    authenticatorDll.text = _controllerDefaultPath("cobalt.dll");
    firstRun.value = true;
  }

  String _controllerDefaultPath(String name) => "${assetsDirectory.path}\\dlls\\$name";
}
