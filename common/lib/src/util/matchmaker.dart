import 'dart:io';

import 'package:ini/ini.dart';

import 'package:reboot_common/common.dart';

Future<void> writeMatchmakingIp(String text) async {
  var file = File("${assetsDirectory.path}\\lawin\\Config\\config.ini");
  if(!file.existsSync()){
    return;
  }

  var splitIndex = text.indexOf(":");
  var ip = splitIndex != -1 ? text.substring(0, splitIndex) : text;
  var port = splitIndex != -1 ? text.substring(splitIndex + 1) : "7777";
  var config = Config.fromString(file.readAsStringSync());
  config.set("GameServer", "ip", ip);
  config.set("GameServer", "port", port);
  file.writeAsStringSync(config.toString());
}

Future<bool> isMatchmakerPortFree() async => isPortFree(int.parse(kDefaultMatchmakerPort));

Future<bool> freeMatchmakerPort() async {
  var releaseBat = File("${assetsDirectory.path}\\lawin\\kill_matchmaker.bat");
  await Process.run(releaseBat.path, []);
  var standardResult = await isMatchmakerPortFree();
  if(standardResult) {
    return true;
  }

  var elevatedResult = await runElevatedProcess(releaseBat.path, "");
  if(!elevatedResult) {
    return false;
  }

  return await isMatchmakerPortFree();
}