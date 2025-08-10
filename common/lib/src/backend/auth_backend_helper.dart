import 'dart:async';
import 'dart:io';

import 'package:ini/ini.dart';
import 'package:reboot_common/common.dart';

import 'package:shelf/shelf_io.dart';
import 'package:shelf_proxy/shelf_proxy.dart';
import 'package:sync/semaphore.dart';

final Directory backendDirectory = Directory("${assetsDirectory.path}\\backend");
final File backendStartExecutable = File("${backendDirectory.path}\\lawinserver.exe");
final File matchmakerConfigFile = File("${backendDirectory.path}\\Config\\config.ini");
final Semaphore _semaphore = Semaphore();
String? _lastIp;
String? _lastPort;

typedef BackendErrorHandler = void Function(String);

Stream<AuthBackendResult> startAuthBackend({
  required AuthBackendType type,
  required String host,
  required String port,
  required bool detached,
  required BackendErrorHandler? onError
}) async* {
  Process? process;
  HttpServer? server;
  try {
    host = host.trim();
    port = port.trim();
    if(type != AuthBackendType.local || port != kDefaultBackendPort.toString()) {
      yield AuthBackendResult(AuthBackendResultType.starting);
    }

    if (host.isEmpty) {
      yield AuthBackendResult(AuthBackendResultType.startMissingHostError);
      return;
    }

    if (port.isEmpty) {
      yield AuthBackendResult(AuthBackendResultType.startMissingPortError);
      return;
    }

    final portNumber = int.tryParse(port);
    if (portNumber == null) {
      yield AuthBackendResult(AuthBackendResultType.startIllegalPortError);
      return;
    }

    if ((type != AuthBackendType.local || port != kDefaultBackendPort.toString()) && !(await isAuthBackendPortFree())) {
      yield AuthBackendResult(AuthBackendResultType.startFreeingPort);
      final result = await freeAuthBackendPort();
      if(!result) {
        yield AuthBackendResult(AuthBackendResultType.startFreePortError);
        return;
      }

      yield AuthBackendResult(AuthBackendResultType.startFreePortSuccess);
    }

    switch(type){
      case AuthBackendType.embedded:
        process = await _startEmbedded(detached, onError: onError);
        yield AuthBackendResult(AuthBackendResultType.startedImplementation, implementation: AuthBackendImplementation(process: process));
        break;
      case AuthBackendType.remote:
        yield AuthBackendResult(AuthBackendResultType.startPingingRemote);
        final uriResult = await _ping(host, portNumber);
        if(uriResult == null) {
          yield AuthBackendResult(AuthBackendResultType.startPingError);
          return;
        }

        server = await _startRemote(uriResult);
        yield AuthBackendResult(AuthBackendResultType.startedImplementation, implementation: AuthBackendImplementation(server: server));
        break;
      case AuthBackendType.local:
        if(portNumber != kDefaultBackendPort) {
          yield AuthBackendResult(AuthBackendResultType.startPingingLocal);
          final uriResult = await _ping(kDefaultBackendHost, portNumber);
          if(uriResult == null) {
            yield AuthBackendResult(AuthBackendResultType.startPingError);
            return;
          }

          server = await _startRemote(Uri.parse("http://$kDefaultBackendHost:$port"));
          yield AuthBackendResult(AuthBackendResultType.startedImplementation, implementation: AuthBackendImplementation(server: server));
        }
        break;
    }

    yield AuthBackendResult(AuthBackendResultType.startPingingLocal);
    final uriResult = await _ping(kDefaultBackendHost, kDefaultBackendPort);
    if(uriResult == null) {
      yield AuthBackendResult(AuthBackendResultType.startPingError);
      process?.kill(ProcessSignal.sigterm);
      server?.close(force: true);
      return;
    }

    yield AuthBackendResult(AuthBackendResultType.startSuccess);
  }catch(error, stackTrace) {
    yield AuthBackendResult(
        AuthBackendResultType.startError,
        error: error,
        stackTrace: stackTrace
    );
    process?.kill(ProcessSignal.sigterm);
    server?.close(force: true);
  }
}

Future<Process> _startEmbedded(bool detached, {BackendErrorHandler? onError}) async {
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
    process.exitCode.then((exitCode) {
      if(!killed) {
        log("[BACKEND] Exit code: $exitCode");
        onError?.call("Exit code: $exitCode");
        killed = true;
      }
    });
  }
  return process;
}

Future<HttpServer> _startRemote(Uri uri) async => await serve(proxyHandler(uri), kDefaultBackendHost, kDefaultBackendPort);

Stream<AuthBackendResult> stopAuthBackend({required AuthBackendType type, required AuthBackendImplementation? implementation}) async* {
  yield AuthBackendResult(AuthBackendResultType.stopping);
  try{
    switch(type){
      case AuthBackendType.embedded:
        final process = implementation?.process;
        if(process != null) {
          Process.killPid(process.pid, ProcessSignal.sigterm);
        }
        break;
      case AuthBackendType.remote:
        await implementation?.server?.close(force: true);
        break;
      case AuthBackendType.local:
        await implementation?.server?.close(force: true);
        break;
    }
    yield AuthBackendResult(AuthBackendResultType.stopSuccess);
  }catch(error, stackTrace){
    yield AuthBackendResult(
        AuthBackendResultType.stopError,
        error: error,
        stackTrace: stackTrace
    );
  }
}

Future<bool> isAuthBackendPortFree() async => await _ping(kDefaultBackendHost, kDefaultBackendPort) == null;

Future<Uri?> _ping(String host, int port, [bool https=false]) async {
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
    return https || declaredScheme != null || isLocalHost(host) ? null : await _ping(host, port, true);
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

Future<bool> freeAuthBackendPort() async {
  await killProcessByPort(kDefaultBackendPort);
  await killProcessByPort(kDefaultXmppPort);
  final standardResult = await isAuthBackendPortFree();
  if(standardResult) {
    return true;
  }

  return false;
}

Future<void> writeAuthBackendMatchmakingIp(String text) async {
  final exists = await matchmakerConfigFile.exists();
  if(!exists) {
    return;
  }

  _semaphore.acquire();
  final splitIndex = text.indexOf(":");
  final ip = splitIndex != -1 ? text.substring(0, splitIndex) : text;
  var port = splitIndex != -1 ? text.substring(splitIndex + 1) : kDefaultGameServerPort;
  if(port.isBlankOrEmpty) {
    port = kDefaultGameServerPort;
  }

  _lastIp = ip;
  _lastPort = port;
  final config = Config.fromString(await matchmakerConfigFile.readAsString());
  config.set("GameServer", "ip", ip);
  config.set("GameServer", "port", port);
  await matchmakerConfigFile.writeAsString(config.toString(), flush: true);
}