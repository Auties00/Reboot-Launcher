import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class SettingsController extends GetxController {
  late final GetStorage _storage;
  late final String originalDll;
  late final TextEditingController gameServerDll;
  late final TextEditingController unrealEngineConsoleDll;
  late final TextEditingController backendDll;
  late final TextEditingController memoryLeakDll;
  late final TextEditingController gameServerPort;
  late final RxBool firstRun;
  late final RxString language;
  late final Rx<ThemeMode> themeMode;
  late double width;
  late double height;
  late double? offsetX;
  late double? offsetY;

  SettingsController() {
    _storage = GetStorage("settings");
    gameServerDll = _createController("game_server", "reboot.dll");
    unrealEngineConsoleDll = _createController("unreal_engine_console", "console.dll");
    backendDll = _createController("backend", "cobalt.dll");
    memoryLeakDll = _createController("memory_leak", "memoryleak.dll");
    gameServerPort = TextEditingController(text: _storage.read("game_server_port") ?? kDefaultGameServerPort);
    gameServerPort.addListener(() => _storage.write("game_server_port", gameServerPort.text));
    width = _storage.read("width") ?? kDefaultWindowWidth;
    height = _storage.read("height") ?? kDefaultWindowHeight;
    offsetX = _storage.read("offset_x");
    offsetY = _storage.read("offset_y");
    firstRun = RxBool(_storage.read("first_run_new1") ?? true);
    firstRun.listen((value) => _storage.write("first_run_new1", value));
    themeMode = Rx(ThemeMode.values.elementAt(_storage.read("theme") ?? 0));
    themeMode.listen((value) => _storage.write("theme", value.index));
    language = RxString(_storage.read("language") ?? currentLocale);
    language.listen((value) => _storage.write("language", value));
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
    offsetX = position.dx;
    offsetY = position.dy;
    _storage.write("offset_x", offsetX);
    _storage.write("offset_y", offsetY);
  }

  void reset(){
    gameServerDll.text = _controllerDefaultPath("reboot.dll");
    unrealEngineConsoleDll.text = _controllerDefaultPath("console.dll");
    backendDll.text = _controllerDefaultPath("cobalt.dll");
    gameServerPort.text = kDefaultGameServerPort;
    firstRun.value = true;
  }

  String _controllerDefaultPath(String name) => "${assetsDirectory.path}\\dlls\\$name";
}
