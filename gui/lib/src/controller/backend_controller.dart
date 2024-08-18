import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/util/keyboard.dart';

class BackendController extends GetxController {
  static const String storageName = "backend_storage";
  static const PhysicalKeyboardKey _kDefaultConsoleKey = PhysicalKeyboardKey(0x00070041);

  late final GetStorage? _storage;
  late final TextEditingController host;
  late final TextEditingController port;
  late final Rx<ServerType> type;
  late final TextEditingController gameServerAddress;
  late final FocusNode gameServerAddressFocusNode;
  late final Rx<PhysicalKeyboardKey> consoleKey;
  late final RxBool started;
  late final RxBool detached;
  StreamSubscription? worker;
  int? embeddedProcessPid;
  HttpServer? localServer;
  HttpServer? remoteServer;

  BackendController() {
    _storage = appWithNoStorage ? null : GetStorage(storageName);
    started = RxBool(false);
    type = Rx(ServerType.values.elementAt(_storage?.read("type") ?? 0));
    type.listen((value) {
      host.text = _readHost();
      port.text = _readPort();
      _storage?.write("type", value.index);
      if (!started.value) {
        return;
      }

      stop();
    });
    host = TextEditingController(text: _readHost());
    host.addListener(() =>
        _storage?.write("${type.value.name}_host", host.text));
    port = TextEditingController(text: _readPort());
    port.addListener(() =>
        _storage?.write("${type.value.name}_port", port.text));
    detached = RxBool(_storage?.read("detached") ?? false);
    detached.listen((value) => _storage?.write("detached", value));
    final address = _storage?.read("game_server_address");
    gameServerAddress = TextEditingController(text: address == null || address.isEmpty ? "127.0.0.1" : address);
    var lastValue = gameServerAddress.text;
    writeMatchmakingIp(lastValue);
    gameServerAddress.addListener(() {
      var newValue = gameServerAddress.text;
      if(newValue.trim().toLowerCase() == lastValue.trim().toLowerCase()) {
        return;
      }

      lastValue = newValue;
      gameServerAddress.selection = TextSelection.collapsed(offset: newValue.length);
      _storage?.write("game_server_address", newValue);
      writeMatchmakingIp(newValue);
    });
    watchMatchmakingIp().listen((event) {
      if(event != null && gameServerAddress.text != event) {
        gameServerAddress.text = event;
      }
    });
    gameServerAddressFocusNode = FocusNode();
    consoleKey = Rx(_readConsoleKey());
    _writeConsoleKey(consoleKey.value);
    consoleKey.listen((newValue) {
      _storage?.write("console_key", newValue.usbHidUsage);
      _writeConsoleKey(newValue);
    });
  }

  PhysicalKeyboardKey _readConsoleKey() {
    final consoleKeyValue = _storage?.read("console_key");
    if(consoleKeyValue == null) {
      return _kDefaultConsoleKey;
    }

    final consoleKeyNumber = int.tryParse(consoleKeyValue.toString());
    if(consoleKeyNumber == null) {
      return _kDefaultConsoleKey;
    }

    final consoleKey = PhysicalKeyboardKey(consoleKeyNumber);
    if(!consoleKey.isUnrealEngineKey) {
      return _kDefaultConsoleKey;
    }

    return consoleKey;
  }

  Future<void> _writeConsoleKey(PhysicalKeyboardKey keyValue) async {
    final defaultInput = File("${backendDirectory.path}\\CloudStorage\\DefaultInput.ini");
    await defaultInput.parent.create(recursive: true);
    await defaultInput.writeAsString("[/Script/Engine.InputSettings]\n+ConsoleKeys=Tilde\n+ConsoleKeys=${keyValue.unrealEngineName}", flush: true);
  }

  void joinLocalhost() {
    gameServerAddress.text = kDefaultGameServerHost;
  }

  void reset() async {
    type.value = ServerType.values.elementAt(0);
    for (final type in ServerType.values) {
      _storage?.write("${type.name}_host", null);
      _storage?.write("${type.name}_port", null);
    }

    host.text = type.value != ServerType.remote ? kDefaultBackendHost : "";
    port.text = kDefaultBackendPort.toString();
    gameServerAddress.text = "127.0.0.1";
    consoleKey.value = _kDefaultConsoleKey;
    detached.value = false;
  }

  String _readHost() {
    String? value = _storage?.read("${type.value.name}_host");
    if (value != null && value.isNotEmpty) {
      return value;
    }

    if (type.value != ServerType.remote) {
      return kDefaultBackendHost;
    }

    return "";
  }

  String _readPort() =>
      _storage?.read("${type.value.name}_port") ?? kDefaultBackendPort.toString();

  Stream<ServerResult> start({required void Function() onExit, required void Function(String) onError}) async* {
    try {
      if(started.value) {
        return;
      }

      final serverType = type.value;
      final hostData = this.host.text.trim();
      final portData = this.port.text.trim();
      started.value = true;
      if(serverType != ServerType.local || portData != kDefaultBackendPort.toString()) {
        yield ServerResult(ServerResultType.starting);
      }

      if (hostData.isEmpty) {
        yield ServerResult(ServerResultType.missingHostError);
        started.value = false;
        return;
      }

      if (portData.isEmpty) {
        yield ServerResult(ServerResultType.missingPortError);
        started.value = false;
        return;
      }

      final portNumber = int.tryParse(portData);
      if (portNumber == null) {
        yield ServerResult(ServerResultType.illegalPortError);
        started.value = false;
        return;
      }

      if ((serverType != ServerType.local || portData != kDefaultBackendPort.toString()) && !(await isBackendPortFree())) {
        yield ServerResult(ServerResultType.freeingPort);
        final result = await freeBackendPort();
        yield ServerResult(result ? ServerResultType.freePortSuccess : ServerResultType.freePortError);
        if(!result) {
          started.value = false;
          return;
        }
      }

      switch(serverType){
        case ServerType.embedded:
          final process = await startEmbeddedBackend(detached.value, onError: (errorMessage) {
            if(started.value) {
              started.value = false;
              onError(errorMessage);
            }
          });
          watchProcess(process.pid).then((_) {
            if(started.value) {
              started.value = false;
              onExit();
            }
          });
          embeddedProcessPid = process.pid;
          break;
        case ServerType.remote:
          yield ServerResult(ServerResultType.pingingRemote);
          final uriResult = await pingBackend(hostData, portNumber);
          if(uriResult == null) {
            yield ServerResult(ServerResultType.pingError);
            started.value = false;
            return;
          }

          remoteServer = await startRemoteBackendProxy(uriResult);
          break;
        case ServerType.local:
          if(portNumber != kDefaultBackendPort) {
            yield ServerResult(ServerResultType.pingingLocal);
            final uriResult = await pingBackend(kDefaultBackendHost, portNumber);
            if(uriResult == null) {
              yield ServerResult(ServerResultType.pingError);
              started.value = false;
              return;
            }

            localServer = await startRemoteBackendProxy(Uri.parse("http://$kDefaultBackendHost:$portData"));
          }else {
            // If the local server is running on port 3551 there is no reverse proxy running
            // We only need to check if everything is working
            started.value = false;
          }

          break;
      }

      yield ServerResult(ServerResultType.pingingLocal);
      final uriResult = await pingBackend(kDefaultBackendHost, kDefaultBackendPort);
      if(uriResult == null) {
        yield ServerResult(ServerResultType.pingError);
        remoteServer?.close(force: true);
        localServer?.close(force: true);
        started.value = false;
        return;
      }

      yield ServerResult(ServerResultType.startSuccess);
    }catch(error, stackTrace) {
      yield ServerResult(
          ServerResultType.startError,
          error: error,
          stackTrace: stackTrace
      );
      remoteServer?.close(force: true);
      localServer?.close(force: true);
      started.value = false;
    }
  }

  Stream<ServerResult> stop() async* {
    if(!started.value) {
      return;
    }

    yield ServerResult(ServerResultType.stopping);
    started.value = false;
    try{
      switch(type()){
        case ServerType.embedded:
          final embeddedProcessPid = this.embeddedProcessPid;
          if(embeddedProcessPid != null) {
            Process.killPid(embeddedProcessPid, ProcessSignal.sigterm);
            this.embeddedProcessPid = null;
          }
          break;
        case ServerType.remote:
          await remoteServer?.close(force: true);
          remoteServer = null;
          break;
        case ServerType.local:
          await localServer?.close(force: true);
          localServer = null;
          break;
      }
      yield ServerResult(ServerResultType.stopSuccess);
    }catch(error, stackTrace){
      yield ServerResult(
          ServerResultType.stopError,
          error: error,
          stackTrace: stackTrace
      );
      started.value = true;
    }
  }

  Stream<ServerResult> toggle({required void Function() onExit, required void Function(String) onError}) async* {
    if(started()) {
      yield* stop();
    }else {
      yield* start(
        onExit: onExit,
        onError: onError
      );
    }
  }
}