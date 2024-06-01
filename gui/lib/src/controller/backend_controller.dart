import 'dart:io';

import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class BackendController extends ServerController {
  late RxBool detached;

  BackendController() : super() {
    detached = RxBool(storage.read("detached") ?? false);
    detached.listen((value) => storage.write("detached", value));
  }

  @override
  String get controllerName => translations.backendName.toLowerCase();

  @override
  String get storageName => "backend";

  @override
  String get defaultHost => kDefaultBackendHost;

  @override
  int get defaultPort => kDefaultBackendPort;

  @override
  Future<bool> get isPortFree => isBackendPortFree();

  @override
  Future<bool> freePort() => freeBackendPort();

  @override
  RebootPageType get pageType => RebootPageType.backend;

  @override
  Future<Process> startEmbeddedInternal() => startEmbeddedBackend(detached.value);

  @override
  Future<Uri?> pingServer(String host, int port) => pingBackend(host, port);
}