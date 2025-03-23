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

Stream<ServerResult> startBackend({required ServerType type, required String host, required String port, required bool detached, required void Function(String) onError}) async* {
  Process? process;
  HttpServer? server;
  try {
    host = host.trim();
    port = port.trim();
    if(type != ServerType.local || port != kDefaultBackendPort.toString()) {
      yield ServerResult(ServerResultType.starting);
    }

    if (host.isEmpty) {
      yield ServerResult(ServerResultType.startMissingHostError);
      return;
    }

    if (port.isEmpty) {
      yield ServerResult(ServerResultType.startMissingPortError);
      return;
    }

    final portNumber = int.tryParse(port);
    if (portNumber == null) {
      yield ServerResult(ServerResultType.startIllegalPortError);
      return;
    }

    if ((type != ServerType.local || port != kDefaultBackendPort.toString()) && !(await isBackendPortFree())) {
      yield ServerResult(ServerResultType.startFreeingPort);
      final result = await freeBackendPort();
      if(!result) {
        yield ServerResult(ServerResultType.startFreePortError);
        return;
      }

      yield ServerResult(ServerResultType.startFreePortSuccess);
    }

    switch(type){
      case ServerType.embedded:
        process = await startEmbeddedBackend(detached, onError: onError);
        yield ServerResult(ServerResultType.startedImplementation, implementation: ServerImplementation(process: process));
        break;
      case ServerType.remote:
        yield ServerResult(ServerResultType.startPingingRemote);
        final uriResult = await pingBackend(host, portNumber);
        if(uriResult == null) {
          yield ServerResult(ServerResultType.startPingError);
          return;
        }

        server = await startRemoteBackendProxy(uriResult);
        yield ServerResult(ServerResultType.startedImplementation, implementation: ServerImplementation(server: server));
        break;
      case ServerType.local:
        if(portNumber != kDefaultBackendPort) {
          yield ServerResult(ServerResultType.startPingingLocal);
          final uriResult = await pingBackend(kDefaultBackendHost, portNumber);
          if(uriResult == null) {
            yield ServerResult(ServerResultType.startPingError);
            return;
          }

          server = await startRemoteBackendProxy(Uri.parse("http://$kDefaultBackendHost:$port"));
          yield ServerResult(ServerResultType.startedImplementation, implementation: ServerImplementation(server: server));
        }
        break;
    }

    yield ServerResult(ServerResultType.startPingingLocal);
    final uriResult = await pingBackend(kDefaultBackendHost, kDefaultBackendPort);
    if(uriResult == null) {
      yield ServerResult(ServerResultType.startPingError);
      process?.kill(ProcessSignal.sigterm);
      server?.close(force: true);
      return;
    }

    yield ServerResult(ServerResultType.startSuccess);
  }catch(error, stackTrace) {
    yield ServerResult(
        ServerResultType.startError,
        error: error,
        stackTrace: stackTrace
    );
    process?.kill(ProcessSignal.sigterm);
    server?.close(force: true);
  }
}

Stream<ServerResult> stopBackend({required ServerType type, required ServerImplementation? implementation}) async* {
  yield ServerResult(ServerResultType.stopping);
  try{
    switch(type){
      case ServerType.embedded:
        final process = implementation?.process;
        if(process != null) {
          Process.killPid(process.pid, ProcessSignal.sigterm);
        }
        break;
      case ServerType.remote:
        await implementation?.server?.close(force: true);
        break;
      case ServerType.local:
        await implementation?.server?.close(force: true);
        break;
    }
    yield ServerResult(ServerResultType.stopSuccess);
  }catch(error, stackTrace){
    yield ServerResult(
        ServerResultType.stopError,
        error: error,
        stackTrace: stackTrace
    );
  }
}

Future<Process> startEmbeddedBackend(bool detached, {void Function(String)? onError}) async {
  final process = await startProcess(
    executable: backendStartExecutable,
    window: detached,
  );
  process.stdOutput.listen((message) => log("[BACKEND] Message: $message"));
  var killed = false;
  process.stdError.listen((error) {
    if(!killed) {
      log("[BACKEND] Error: $error");
      killed = true;
      process.kill(ProcessSignal.sigterm);
      onError?.call(error);
    }
  });
  if(!detached) {
    process.exitCode.then((exitCode) => log("[BACKEND] Exit code: $exitCode"));
  }
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
  }catch(error) {
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