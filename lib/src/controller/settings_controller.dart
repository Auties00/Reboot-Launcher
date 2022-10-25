
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:ini/ini.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/server.dart';

class SettingsController extends GetxController {
  late final GetStorage _storage;
  late final String originalDll;
  late final TextEditingController rebootDll;
  late final TextEditingController consoleDll;
  late final TextEditingController craniumDll;
  late final TextEditingController matchmakingIp;

  SettingsController() {
    _storage = GetStorage("settings");

    rebootDll = _createController("reboot", "reboot.dll");
    consoleDll = _createController("console", "console.dll");
    craniumDll = _createController("cranium", "cranium.dll");
    matchmakingIp = TextEditingController(text: _storage.read("ip") ?? "127.0.0.1");
    matchmakingIp.addListener(() async {
      var text = matchmakingIp.text;
      _storage.write("ip", text);
      if(await serverConfig.exists()){
        var config = Config.fromString(await serverConfig.readAsString());
        if(text.contains(":")){
          config.set("GameServer", "ip", text.substring(0, text.indexOf(":")));
          config.set("GameServer", "port", text.substring(text.indexOf(":") + 1));
        }else {
          config.set("GameServer", "ip", text);
          config.set("GameServer", "port", "7777");
        }

        serverConfig.writeAsString(config.toString());
      }
    });
  }

  TextEditingController _createController(String key, String name) {
    loadBinary(name, true);

    var controller = TextEditingController(text: _storage.read(key) ?? "$safeBinariesDirectory\\$name");
    controller.addListener(() => _storage.write(key, controller.text));

    return controller;
  }
}
