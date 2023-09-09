import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:sync/semaphore.dart';

abstract class ServerController extends GetxController {
  late final GetStorage storage;
  late final TextEditingController host;
  late final TextEditingController port;
  late final Rx<ServerType> type;
  late final Semaphore semaphore;
  late RxBool started;
  late RxBool detached;
  int? embeddedServerPid;
  HttpServer? localServer;
  HttpServer? remoteServer;

  ServerController() {
    storage = GetStorage(storageName);
    started = RxBool(false);
    type = Rx(ServerType.values.elementAt(storage.read("type") ?? 0));
    type.listen((value) {
      host.text = _readHost();
      port.text = _readPort();
      storage.write("type", value.index);
      if (!started.value) {
        return;
      }

      stop();
    });
    host = TextEditingController(text: _readHost());
    host.addListener(() =>
        storage.write("${type.value.name}_host", host.text));
    port = TextEditingController(text: _readPort());
    port.addListener(() =>
        storage.write("${type.value.name}_port", port.text));
    detached = RxBool(storage.read("detached") ?? false);
    detached.listen((value) => storage.write("detached", value));
    semaphore = Semaphore();
  }

  String get controllerName;

  String get storageName;

  String get defaultHost;

  String get defaultPort;

  Future<Uri?> pingServer(String host, String port);
  
  Future<bool> get isPortFree;

  Future<bool> get isPortTaken async => !(await isPortFree);

  Future<bool> freePort();

  @protected
  Future<int> startEmbeddedInternal();

  void reset() async {
    type.value = ServerType.values.elementAt(0);
    for (var type in ServerType.values) {
      storage.write("${type.name}_host", null);
      storage.write("${type.name}_port", null);
    }

    host.text = type.value != ServerType.remote ? defaultHost : "";
    port.text = defaultPort;
    detached.value = false;
  }

  String _readHost() {
    String? value = storage.read("${type.value.name}_host");
    return value != null && value.isNotEmpty ? value
        : type.value != ServerType.remote ? defaultHost : "";
  }

  String _readPort() =>
      storage.read("${type.value.name}_port") ?? defaultPort;

  Stream<ServerResult> start() async* {
    if(started.value) {
      return;
    }

    yield ServerResult(ServerResultType.starting);
    started.value = true;
    try {
      var host = this.host.text.trim();
      if (host.isEmpty) {
        yield ServerResult(ServerResultType.missingHostError);
        started.value = false;
        return;
      }

      var port = this.port.text.trim();
      if (port.isEmpty) {
        yield ServerResult(ServerResultType.missingPortError);
        started.value = false;
        return;
      }

      var portNumber = int.tryParse(port);
      if (portNumber == null) {
        yield ServerResult(ServerResultType.illegalPortError);
        started.value = false;
        return;
      }

      if (type() != ServerType.local && await isPortTaken) {
        yield ServerResult(ServerResultType.freeingPort);
        var result = await freePort();
        yield ServerResult(result ? ServerResultType.freePortSuccess : ServerResultType.freePortError);
        if(!result) {
          started.value = false;
          return;
        }
      }
      switch(type()){
        case ServerType.embedded:
          embeddedServerPid = await startEmbeddedInternal();
          break;
        case ServerType.remote:
          yield ServerResult(ServerResultType.pingingRemote);
          var uriResult = await pingServer(host, port);
          if(uriResult == null) {
            yield ServerResult(ServerResultType.pingError);
            started.value = false;
            return;
          }

          remoteServer = await startRemoteAuthenticatorProxy(uriResult);
          break;
        case ServerType.local:
          if(port != defaultPort) {
            localServer = await startRemoteAuthenticatorProxy(Uri.parse("http://$defaultHost:$defaultPort"));
          }

          break;
      }

      yield ServerResult(ServerResultType.pingingLocal);
      var uriResult = await pingServer(defaultHost, defaultPort);
      if(uriResult == null) {
        yield ServerResult(ServerResultType.pingError);
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
          Process.killPid(embeddedServerPid!, ProcessSignal.sigabrt);
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

  Stream<ServerResult> restart() async* {
    await resetWinNat();
    if(started()) {
      yield* stop();
    }

    yield* start();
  }

  Stream<ServerResult> toggle() async* {
    if(started()) {
      yield* stop();
    }else {
      yield* start();
    }
  }
}