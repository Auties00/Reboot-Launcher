import 'dart:async';
import 'dart:io';

import 'package:ini/ini.dart';

import 'package:reboot_common/common.dart';

final matchmakerDirectory = Directory("${assetsDirectory.path}\\matchmaker");
final matchmakerStartExecutable = File("${matchmakerDirectory.path}\\fortmatchmaker.exe");
final matchmakerKillExecutable =  File("${authenticatorDirectory.path}\\kill.bat");

Future<int> startEmbeddedMatchmaker(bool detached) async => startBackgroundProcess(
    executable: matchmakerStartExecutable,
    window: detached
);

Future<void> writeMatchmakingIp(String text) async {
  var file = File("${authenticatorDirectory}\\Config\\config.ini");
  if(!file.existsSync()){
    return;
  }

  var splitIndex = text.indexOf(":");
  var ip = splitIndex != -1 ? text.substring(0, splitIndex) : text;
  var port = splitIndex != -1 ? text.substring(splitIndex + 1) : kDefaultGameServerPort;
  var config = Config.fromString(file.readAsStringSync());
  config.set("GameServer", "ip", ip);
  config.set("GameServer", "port", port);
  file.writeAsStringSync(config.toString());
}

Future<bool> isMatchmakerPortFree() async => isPortFree(int.parse(kDefaultMatchmakerPort));

Future<bool> freeMatchmakerPort() async {
  await Process.run(matchmakerKillExecutable.path, []);
  var standardResult = await isMatchmakerPortFree();
  if(standardResult) {
    return true;
  }

  var elevatedResult = await runElevatedProcess(matchmakerKillExecutable.path, "");
  if(!elevatedResult) {
    return false;
  }

  return await isMatchmakerPortFree();
}

Future<Uri?> pingMatchmaker(String host, String port, [bool wss=false]) async {
  var hostName = _getHostName(host);
  var declaredScheme = _getScheme(host);
  try{
    var uri = Uri(
        scheme: declaredScheme ?? (wss ? "wss" : "ws"),
        host: hostName,
        port: int.parse(port)
    );
    var completer = Completer<bool>();
    var socket = await WebSocket.connect(uri.toString());
    socket.listen(
      (data) {
        if(!completer.isCompleted) {
          completer.complete(true);
        }
      },
      onError: (error) {
        if(!completer.isCompleted) {
          completer.complete(false);
        }
      },
      onDone: () {
        if(!completer.isCompleted) {
          completer.complete(false);
        }
      },
    );
    var result = await completer.future;
    await socket.close();
    return result ? uri : null;
  }catch(_){
    return wss || declaredScheme != null ? null : await pingMatchmaker(host, port, true);
  }
}

String? _getHostName(String host) => host.replaceFirst("ws://", "").replaceFirst("wss://", "");

String? _getScheme(String host) => host.startsWith("ws://") ? "ws" : host.startsWith("wss://") ? "wss" : null;

