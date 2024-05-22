import 'dart:async';
import 'dart:io';

import 'package:ini/ini.dart';
import 'package:reboot_common/common.dart';
import 'package:sync/semaphore.dart';

final matchmakerDirectory = Directory("${assetsDirectory.path}\\matchmaker");
final matchmakerStartExecutable = File("${matchmakerDirectory.path}\\fortmatchmaker.exe");
final matchmakerKillExecutable =  File("${authenticatorDirectory.path}\\kill.bat");
final matchmakerConfigFile = File("${authenticatorDirectory.path}\\Config\\config.ini");
String? _lastIp;
String? _lastPort;
Semaphore _semaphore = Semaphore();

Future<Win32Process> startEmbeddedMatchmaker(bool detached) async => startProcess(
    executable: matchmakerStartExecutable,
    window: detached,
    
);

Stream<String?> watchMatchmakingIp() async* {
  if(!matchmakerConfigFile.existsSync()){
    return;
  }

  var observer = matchmakerConfigFile.parent.watch(events: FileSystemEvent.modify);
  yield* observer.where((event) => event.path == matchmakerConfigFile.path).asyncMap((event) async {
    try {
      var config = Config.fromString(await matchmakerConfigFile.readAsString());
      var ip = config.get("GameServer", "ip");
      if(ip == null) {
        return null;
      }

      var port = config.get("GameServer", "port");
      if(port == null) {
        return null;
      }

      if(_lastIp == ip && _lastPort == port) {
        return null;
      }

      return port == kDefaultGameServerPort ? ip : "$ip:$port";
    }finally {
      try {
        _semaphore.release();
      } on StateError catch(_) {
        // Intended behaviour
      }
    }
  });
}

Future<void> writeMatchmakingIp(String text) async {
  var exists = await matchmakerConfigFile.exists();
  if(!exists) {
    return;
  }

  _semaphore.acquire();
  var splitIndex = text.indexOf(":");
  var ip = splitIndex != -1 ? text.substring(0, splitIndex) : text;
  var port = splitIndex != -1 ? text.substring(splitIndex + 1) : kDefaultGameServerPort;
  if(port.isBlank) {
    port = kDefaultGameServerPort;
  }

  _lastIp = ip;
  _lastPort = port;
  var config = Config.fromString(await matchmakerConfigFile.readAsString());
  config.set("GameServer", "ip", ip);
  config.set("GameServer", "port", port);
  await matchmakerConfigFile.writeAsString(config.toString(), flush: true);
}

Future<bool> isMatchmakerPortFree() async => await pingMatchmaker(kDefaultMatchmakerHost, kDefaultMatchmakerPort) == null;

Future<bool> freeMatchmakerPort() async {
  await killProcessByPort(kDefaultMatchmakerPort);
  final standardResult = await isMatchmakerPortFree();
  if(standardResult) {
    return true;
  }

  return false;
}

Future<Uri?> pingMatchmaker(String host, int port, [bool wss=false]) async {
  var hostName = _getHostName(host);
  var declaredScheme = _getScheme(host);
  try{
    var uri = Uri(
        scheme: declaredScheme ?? (wss ? "wss" : "ws"),
        host: hostName,
        port: port
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
    return wss || declaredScheme != null || isLocalHost(host) ? null : await pingMatchmaker(host, port, true);
  }
}

String? _getHostName(String host) => host.replaceFirst("ws://", "").replaceFirst("wss://", "");

String? _getScheme(String host) => host.startsWith("ws://") ? "ws" : host.startsWith("wss://") ? "wss" : null;

extension StringExtension on String {
  bool get isBlank {
    if(isEmpty) {
      return true;
    }

    for(var char in this.split("")) {
      if(char != " ") {
        return false;
      }
    }

    return true;
  }
}