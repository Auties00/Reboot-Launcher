import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';

class BackendController extends GetxController {
  late final GetStorage? storage;
  late final TextEditingController host;
  late final TextEditingController port;
  late final Rx<ServerType> type;
  late final TextEditingController gameServerAddress;
  late final FocusNode gameServerAddressFocusNode;
  late final RxnString gameServerOwner;
  late final RxBool started;
  late final RxBool detached;
  StreamSubscription? worker;
  HttpServer? localServer;
  HttpServer? remoteServer;

  BackendController() {
    storage = appWithNoStorage ? null : GetStorage("backend");
    started = RxBool(false);
    type = Rx(ServerType.values.elementAt(storage?.read("type") ?? 0));
    type.listen((value) {
      host.text = _readHost();
      port.text = _readPort();
      storage?.write("type", value.index);
      if (!started.value) {
        return;
      }

      stop();
    });
    host = TextEditingController(text: _readHost());
    host.addListener(() =>
        storage?.write("${type.value.name}_host", host.text));
    port = TextEditingController(text: _readPort());
    port.addListener(() =>
        storage?.write("${type.value.name}_port", port.text));
    detached = RxBool(storage?.read("detached") ?? false);
    detached.listen((value) => storage?.write("detached", value));
    gameServerAddress = TextEditingController(text: storage?.read("game_server_address") ?? "127.0.0.1");
    var lastValue = gameServerAddress.text;
    writeMatchmakingIp(lastValue);
    gameServerAddress.addListener(() {
      var newValue = gameServerAddress.text;
      if(newValue.trim().toLowerCase() == lastValue.trim().toLowerCase()) {
        return;
      }

      lastValue = newValue;
      gameServerAddress.selection = TextSelection.collapsed(offset: newValue.length);
      storage?.write("game_server_address", newValue);
      writeMatchmakingIp(newValue);
    });
    watchMatchmakingIp().listen((event) {
      if(event != null && gameServerAddress.text != event) {
        gameServerAddress.text = event;
      }
    });
    gameServerAddressFocusNode = FocusNode();
    gameServerOwner = RxnString(storage?.read("game_server_owner"));
    gameServerOwner.listen((value) => storage?.write("game_server_owner", value));
  }

  void reset() async {
    type.value = ServerType.values.elementAt(0);
    for (final type in ServerType.values) {
      storage?.write("${type.name}_host", null);
      storage?.write("${type.name}_port", null);
    }

    host.text = type.value != ServerType.remote ? kDefaultBackendHost : "";
    port.text = kDefaultBackendPort.toString();
    detached.value = false;
  }

  String _readHost() {
    String? value = storage?.read("${type.value.name}_host");
    if (value != null && value.isNotEmpty) {
      return value;
    }

    if (type.value != ServerType.remote) {
      return kDefaultBackendHost;
    }

    return "";
  }

  String _readPort() =>
      storage?.read("${type.value.name}_port") ?? kDefaultBackendPort.toString();

  Stream<ServerResult> start() async* {
    try {
      if(started.value) {
        return;
      }

      final hostData = this.host.text.trim();
      final portData = this.port.text.trim();
      if(type() != ServerType.local) {
        started.value = true;
        yield ServerResult(ServerResultType.starting);
      }else {
        started.value = false;
        if(portData != kDefaultBackendPort.toString()) {
          yield ServerResult(ServerResultType.starting);
        }
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

      if ((type() != ServerType.local || portData != kDefaultBackendPort.toString()) && !(await isBackendPortFree())) {
        yield ServerResult(ServerResultType.freeingPort);
        final result = await freeBackendPort();
        yield ServerResult(result ? ServerResultType.freePortSuccess : ServerResultType.freePortError);
        if(!result) {
          started.value = false;
          return;
        }
      }

      switch(type()){
        case ServerType.embedded:
          final process = await startEmbeddedBackend(detached.value);
          final processPid = process.pid;
          watchProcess(processPid).then((value) {
            if(started()) {
              started.value = false;
            }
          });
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
          if(portData != kDefaultBackendPort.toString()) {
            localServer = await startRemoteBackendProxy(Uri.parse("http://$kDefaultBackendHost:$portData"));
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
          killProcessByPort(kDefaultBackendPort);
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

  Stream<ServerResult> toggle() async* {
    if(started()) {
      yield* stop();
    }else {
      yield* start();
    }
  }
}