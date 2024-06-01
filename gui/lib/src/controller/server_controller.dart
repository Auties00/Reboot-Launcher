import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:sync/semaphore.dart';

abstract class ServerController extends GetxController {
  late final GetStorage storage;
  late final TextEditingController host;
  late final TextEditingController port;
  late final Rx<ServerType> type;
  late final Semaphore semaphore;
  late RxBool started;
  StreamSubscription? worker;
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
    semaphore = Semaphore();
  }

  String get controllerName;

  String get storageName;

  String get defaultHost;

  int get defaultPort;

  Future<Uri?> pingServer(String host, int port);
  
  Future<bool> get isPortFree;

  Future<bool> get isPortTaken async => !(await isPortFree);

  RebootPageType get pageType;

  Future<bool> freePort();

  @protected
  Future<Process> startEmbeddedInternal();

  void reset() async {
    type.value = ServerType.values.elementAt(0);
    for (final type in ServerType.values) {
      storage.write("${type.name}_host", null);
      storage.write("${type.name}_port", null);
    }

    host.text = type.value != ServerType.remote ? defaultHost : "";
    port.text = defaultPort.toString();
    detached.value = false;
  }

  String _readHost() {
    String? value = storage.read("${type.value.name}_host");
    return value != null && value.isNotEmpty ? value
        : type.value != ServerType.remote ? defaultHost : "";
  }

  String _readPort() =>
      storage.read("${type.value.name}_port") ?? defaultPort.toString();

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
        if(portData != defaultPort.toString()) {
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

      if ((type() != ServerType.local || portData != defaultPort.toString()) && await isPortTaken) {
        yield ServerResult(ServerResultType.freeingPort);
        final result = await freePort();
        yield ServerResult(result ? ServerResultType.freePortSuccess : ServerResultType.freePortError);
        if(!result) {
          started.value = false;
          return;
        }
      }

      switch(type()){
        case ServerType.embedded:
          final process = await startEmbeddedInternal();
          final processPid = process.pid;
          watchProcess(processPid).then((value) {
            if(started()) {
              started.value = false;
            }
          });
          break;
        case ServerType.remote:
          yield ServerResult(ServerResultType.pingingRemote);
          final uriResult = await pingServer(hostData, portNumber);
          if(uriResult == null) {
            yield ServerResult(ServerResultType.pingError);
            started.value = false;
            return;
          }

          remoteServer = await startRemoteBackendProxy(uriResult);
          break;
        case ServerType.local:
          if(portData != defaultPort.toString()) {
            localServer = await startRemoteBackendProxy(Uri.parse("http://$defaultHost:$portData"));
          }

          break;
      }

      yield ServerResult(ServerResultType.pingingLocal);
      final uriResult = await pingServer(defaultHost, defaultPort);
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
          killProcessByPort(defaultPort);
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