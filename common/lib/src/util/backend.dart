import 'dart:async';
import 'dart:io';

import 'package:ini/ini.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_common/src/extension/types.dart';
import 'package:shelf/shelf_io.dart';
import 'package:shelf_proxy/shelf_proxy.dart';
import 'package:sync/semaphore.dart';

final Directory backendDirectory = Directory("${assetsDirectory.path}\\backend");
final File backendStartExecutable = File("${backendDirectory.path}\\lawinserver.exe");
final File matchmakerConfigFile = File("${backendDirectory.path}\\Config\\config.ini");
final Semaphore _semaphore = Semaphore();
String? _lastIp;
String? _lastPort;

Future<Process> startEmbeddedBackend(bool detached) async {
  final process = await startProcess(
    executable: backendStartExecutable,
    window: detached,
  );
  process.stdOutput.listen((message) => log("[BACKEND] Message: $message"));
  process.stdError.listen((error) => log("[BACKEND] Error: $error"));
  process.exitCode.then((exitCode) => log("[BACKEND] Exit code: $exitCode"));
  return process;
}

Future<HttpServer> startRemoteBackendProxy(Uri uri) async => await serve(proxyHandler(uri), kDefaultBackendHost, kDefaultBackendPort);

Future<bool> isBackendPortFree() async => await pingBackend(kDefaultBackendHost, kDefaultBackendPort) == null;

Future<bool> freeBackendPort() async {
  await killProcessByPort(kDefaultBackendPort);
  await killProcessByPort(kDefaultXmppPort);
  final standardResult = await isBackendPortFree();
  if(standardResult) {
    return true;
  }

  return false;
}

Future<Uri?> pingBackend(String host, int port, [bool https=false]) async {
  final hostName = host.replaceFirst("http://", "").replaceFirst("https://", "");
  final declaredScheme = host.startsWith("http://") ? "http" : host.startsWith("https://") ? "https" : null;
  try{
    final uri = Uri(
        scheme: declaredScheme ?? (https ? "https" : "http"),
        host: hostName,
        port: port,
        path: "unknown"
    );
    log("[BACKEND] Pinging $uri...");
    final client = HttpClient()
      ..connectionTimeout = const Duration(seconds: 10);
    final request = await client.getUrl(uri);
    await request.close().timeout(const Duration(seconds: 10));
    log("[BACKEND] Ping successful");
    return uri;
  }catch(error){
    log("[BACKEND] Cannot ping backend: $error");
    return https || declaredScheme != null || isLocalHost(host) ? null : await pingBackend(host, port, true);
  }
}

Stream<String?> watchMatchmakingIp() async* {
  if(!matchmakerConfigFile.existsSync()){
    return;
  }

  final observer = matchmakerConfigFile.parent.watch(events: FileSystemEvent.modify);
  yield* observer.where((event) => event.path == matchmakerConfigFile.path).asyncMap((event) async {
    try {
      final config = Config.fromString(await matchmakerConfigFile.readAsString());
      final ip = config.get("GameServer", "ip");
      if(ip == null) {
        return null;
      }

      final port = config.get("GameServer", "port");
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
  final exists = await matchmakerConfigFile.exists();
  if(!exists) {
    return;
  }

  _semaphore.acquire();
  final splitIndex = text.indexOf(":");
  final ip = splitIndex != -1 ? text.substring(0, splitIndex) : text;
  var port = splitIndex != -1 ? text.substring(splitIndex + 1) : kDefaultGameServerPort;
  if(port.isBlank) {
    port = kDefaultGameServerPort;
  }

  _lastIp = ip;
  _lastPort = port;
  final config = Config.fromString(await matchmakerConfigFile.readAsString());
  config.set("GameServer", "ip", ip);
  config.set("GameServer", "port", port);
  await matchmakerConfigFile.writeAsString(config.toString(), flush: true);
}