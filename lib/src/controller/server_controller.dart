import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/dialog/server_dialogs.dart';
import 'package:reboot_launcher/src/util/server.dart';

import '../dialog/snackbar.dart';
import '../model/server_type.dart';

class ServerController extends GetxController {
  static const String _serverName = "127.0.0.1";
  static const String _serverPort = "3551";

  late final GetStorage _storage;
  late final TextEditingController host;
  late final TextEditingController port;
  late final Rx<ServerType> type;
  late final RxBool warning;
  late RxBool started;
  late int embeddedServerCounter;
  Process? embeddedServer;
  HttpServer? reverseProxy;

  ServerController() {
    _storage = GetStorage("server");

    type = Rx(ServerType.values.elementAt(_storage.read("type") ?? 0));
    type.listen((value) {
      host.text = _readHost();
      port.text = _readPort();
      _storage.write("type", value.index);

      if(!started.value) {
        return;
      }

      if(value == ServerType.remote){
        reverseProxy?.close(force: true);
        reverseProxy = null;
        started.value = false;
        return;
      }

      stop();
    });

    host = TextEditingController(text: _readHost());
    host.addListener(() => _storage.write("${type.value.id}_host", host.text));

    port = TextEditingController(text: _readPort());
    port.addListener(() => _storage.write("${type.value.id}_port", port.text));

    warning = RxBool(_storage.read("lawin_value") ?? true);
    warning.listen((value) => _storage.write("lawin_value", value));

    started = RxBool(false);

    embeddedServerCounter = 0;
  }

  String _readHost() {
    String? value = _storage.read("${type.value.id}_host");
    return value != null && value.isNotEmpty ? value
        : type.value != ServerType.remote ? _serverName : "";
  }

  String _readPort() {
    return _storage.read("${type.value.id}_port") ?? _serverPort;
  }

  Future<ServerResult> start(bool needsFreePort) async {
    var lastCounter = ++embeddedServerCounter;
    var result = await checkServerPreconditions(host.text, port.text, type.value, needsFreePort);
    if(result.type != ServerResultType.canStart){
      return result;
    }

    try{
      switch(type()){
        case ServerType.embedded:
          await _startEmbeddedServer();
          embeddedServer?.exitCode.then((value) async {
            if (!started() || lastCounter != embeddedServerCounter) {
              return;
            }

            started.value = false;
            await freeLawinPort();
            showUnexpectedError();
          });
          break;
        case ServerType.remote:
          var uriResult = await result.uri!;
          if(uriResult == null){
            return ServerResult(
                type: ServerResultType.cannotPingServer
            );
          }

          reverseProxy = await startRemoteServer(uriResult);
          break;
        case ServerType.local:
          break;
      }
    }catch(error, stackTrace){
      return ServerResult(
          error: error,
          stackTrace: stackTrace,
          type: ServerResultType.unknownError
      );
    }

    var myself = await pingSelf(port.text);
    if(myself == null){
      return ServerResult(
          type: ServerResultType.cannotPingServer,
          pid: embeddedServer?.pid
      );
    }

    return ServerResult(
        type: ServerResultType.started
    );
  }

  Future<void> _startEmbeddedServer() async {
    var result = await startEmbeddedServer();
    if(result != null){
      embeddedServer = result;
      return;
    }

    showMessage("The server is corrupted, trying to fix it");
    await serverLocation.parent.delete(recursive: true);
    await downloadServerInteractive(true);
    await _startEmbeddedServer();
  }

  Future<bool> stop() async {
    started.value = false;
    try{
      switch(type()){
        case ServerType.embedded:
          await freeLawinPort();
          break;
        case ServerType.remote:
          await reverseProxy?.close(force: true);
          break;
        case ServerType.local:
          break;
      }
      return true;
    }catch(_){
      started.value = true;
      return false;
    }
  }
}