import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:path/path.dart' as path;
import 'package:path/path.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/messenger/info_bar.dart';
import 'package:version/version.dart';

class DllController extends GetxController {
  static const String storageName = "v3_dll_storage";

  late final GetStorage? _storage;
  late final TextEditingController customGameServerDll;
  late final TextEditingController unrealEngineConsoleDll;
  late final TextEditingController backendDll;
  late final TextEditingController memoryLeakDll;
  late final TextEditingController gameServerPort;
  late final TextEditingController beforeS20Mirror;
  late final TextEditingController aboveS20Mirror;
  late final RxBool customGameServer;
  late final RxnInt timestamp;
  late final Rx<UpdateStatus> status;

  DllController() {
    _storage = appWithNoStorage ? null : GetStorage(storageName);
    customGameServerDll = _createController("game_server", GameDll.gameServer);
    unrealEngineConsoleDll = _createController("unreal_engine_console", GameDll.console);
    backendDll = _createController("backend", GameDll.auth);
    memoryLeakDll = _createController("memory_leak", GameDll.memoryLeak);
    gameServerPort = TextEditingController(text: _storage?.read("game_server_port") ?? kDefaultGameServerPort);
    gameServerPort.addListener(() => _storage?.write("game_server_port", gameServerPort.text));
    beforeS20Mirror = TextEditingController(text: _storage?.read("before_s20_update_url") ?? kRebootBelowS20DownloadUrl);
    beforeS20Mirror.addListener(() => _storage?.write("before_s20_update_url", beforeS20Mirror.text));
    aboveS20Mirror = TextEditingController(text: _storage?.read("after_s20_update_url") ?? kRebootAboveS20DownloadUrl);
    aboveS20Mirror.addListener(() => _storage?.write("after_s20_update_url", aboveS20Mirror.text));
    status = Rx(UpdateStatus.waiting);
    customGameServer = RxBool(_storage?.read("custom_game_server") ?? false);
    customGameServer.listen((value) => _storage?.write("custom_game_server", value));
    timestamp = RxnInt(_storage?.read("ts"));
    timestamp.listen((value) => _storage?.write("ts", value));
  }

  TextEditingController _createController(String key, GameDll dll) {
    final controller = TextEditingController(text: _storage?.read(key) ?? getDefaultDllPath(dll));
    controller.addListener(() => _storage?.write(key, controller.text));
    return controller;
  }

  void resetGame() {
    customGameServerDll.text = getDefaultDllPath(GameDll.gameServer);
    unrealEngineConsoleDll.text = getDefaultDllPath(GameDll.console);
    backendDll.text = getDefaultDllPath(GameDll.auth);
  }

  void resetServer() {
    gameServerPort.text = kDefaultGameServerPort;
    beforeS20Mirror.text = kRebootBelowS20DownloadUrl;
    aboveS20Mirror.text = kRebootAboveS20DownloadUrl;
    status.value = UpdateStatus.waiting;
    customGameServer.value = false;
    timestamp.value = null;
    updateGameServerDll();
  }

  Future<bool> updateGameServerDll({bool force = false, bool silent = false}) async {
    InfoBarEntry? infoBarEntry;
    try {
      if(customGameServer.value) {
        status.value = UpdateStatus.success;
        return true;
      }

      final needsUpdate = await hasRebootDllUpdate(
          timestamp.value,
          force: force
      );
      if(!needsUpdate) {
        status.value = UpdateStatus.success;
        return true;
      }

      if(!silent) {
        infoBarEntry = showRebootInfoBar(
            translations.downloadingDll("reboot"),
            loading: true,
            duration: null
        );
      }
      final result = await Future.wait(
          [
            downloadRebootDll(rebootBeforeS20DllFile, beforeS20Mirror.text, false),
            downloadRebootDll(rebootAboveS20DllFile, aboveS20Mirror.text, true),
            Future.delayed(const Duration(seconds: 1))
                .then((_) => true)
          ],
          eagerError: false
      ).then((values) => values.reduce((first, second) => first && second));
      if(!result) {
        status.value = UpdateStatus.error;
        showRebootInfoBar(
            translations.downloadDllAntivirus(antiVirusName ?? defaultAntiVirusName, "reboot"),
            duration: infoBarLongDuration,
            severity: InfoBarSeverity.error
        );
        infoBarEntry?.close();
        return false;
      }
      timestamp.value = DateTime.now().millisecondsSinceEpoch;
      status.value = UpdateStatus.success;
      infoBarEntry?.close();
      if(!silent) {
        infoBarEntry = showRebootInfoBar(
            translations.downloadDllSuccess("reboot"),
            severity: InfoBarSeverity.success,
            duration: infoBarShortDuration
        );
      }
      return true;
    }catch(message) {
      infoBarEntry?.close();
      var error = message.toString();
      error = error.contains(": ") ? error.substring(error.indexOf(": ") + 2) : error;
      error = error.toLowerCase();
      status.value = UpdateStatus.error;
      final completer = Completer<bool>();
      infoBarEntry = showRebootInfoBar(
          translations.downloadDllError(error.toString(), "reboot.dll"),
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error,
          onDismissed: () => completer.complete(false),
          action: Button(
            onPressed: () async {
              infoBarEntry?.close();
              final result = updateGameServerDll(
                  force: true,
                  silent: silent
              );
              completer.complete(result);
            },
            child: Text(translations.downloadDllRetry),
          )
      );
      return completer.future;
    }
  }

  (File, bool) getInjectableData(String version, GameDll dll) {
    final defaultPath = canonicalize(getDefaultDllPath(dll));
    switch(dll){
      case GameDll.gameServer:
        if(customGameServer.value) {
          return (File(customGameServerDll.text), true);
        }

        return (_isS20(version) ? rebootAboveS20DllFile : rebootBeforeS20DllFile, false);
      case GameDll.console:
        final ue4ConsoleFile = File(unrealEngineConsoleDll.text);
        return (ue4ConsoleFile, canonicalize(ue4ConsoleFile.path) != defaultPath);
      case GameDll.auth:
        final backendFile = File(backendDll.text);
        return (backendFile, canonicalize(backendFile.path) != defaultPath);
      case GameDll.memoryLeak:
        final memoryFile = File(memoryLeakDll.text);
        return (memoryFile, canonicalize(memoryFile.path) != defaultPath);
    }
  }

  bool _isS20(String version) {
    try {
      return Version.parse(version).major >= 20;
    } on FormatException catch(_) {
      return version.trim().startsWith("20.");
    }
  }

  TextEditingController getDllEditingController(GameDll dll) {
    switch(dll) {
      case GameDll.console:
        return unrealEngineConsoleDll;
      case GameDll.auth:
        return backendDll;
      case GameDll.gameServer:
        return customGameServerDll;
      case GameDll.memoryLeak:
        return memoryLeakDll;
    }
  }

  String getDefaultDllPath(GameDll dll) {
    switch(dll) {
      case GameDll.console:
        return "${dllsDirectory.path}\\console.dll";
      case GameDll.auth:
        return "${dllsDirectory.path}\\cobalt.dll";
      case GameDll.gameServer:
        return "${dllsDirectory.path}\\reboot.dll";
      case GameDll.memoryLeak:
        return "${dllsDirectory.path}\\memory.dll";
    }
  }

  Future<bool> download(GameDll dll, String filePath, {bool silent = false, bool force = false}) async {
    log("[DLL] Asking for $dll at $filePath(silent: $silent, force: $force)");
    InfoBarEntry? entry;
    try {
      if (dll == GameDll.gameServer) {
        return await updateGameServerDll(silent: silent);
      }

      if(!force && File(filePath).existsSync()) {
        log("[DLL] $dll already exists");
        return true;
      }

      log("[DLL] Downloading $dll...");
      final fileNameWithoutExtension = basenameWithoutExtension(filePath);
      if(!silent) {
        log("[DLL] Showing dialog while downloading $dll...");
        entry = showRebootInfoBar(
            translations.downloadingDll(fileNameWithoutExtension),
            loading: true,
            duration: null
        );
      }else {
        log("[DLL] Not showing dialog while downloading $dll...");
      }
      final result = await downloadDependency(dll, filePath);
      if(!result) {
        entry?.close();
        showRebootInfoBar(
            translations.downloadDllAntivirus(antiVirusName ?? defaultAntiVirusName, dll.name),
            duration: infoBarLongDuration,
            severity: InfoBarSeverity.error
        );
        return false;
      }
      log("[DLL] Downloaded $dll");
      entry?.close();
      if(!silent) {
        log("[DLL] Showing success dialog for $dll");
        entry = await showRebootInfoBar(
            translations.downloadDllSuccess(fileNameWithoutExtension),
            severity: InfoBarSeverity.success,
            duration: infoBarShortDuration
        );
      }else {
        log("[DLL] Not showing success dialog for $dll");
      }
      return true;
    }catch(message) {
      log("[DLL] An error occurred while downloading $dll: $message");
      entry?.close();
      var error = message.toString();
      error = error.contains(": ") ? error.substring(error.indexOf(": ") + 2) : error;
      error = error.toLowerCase();
      final completer = Completer<bool>();
      await showRebootInfoBar(
          translations.downloadDllError(error.toString(), dll.name),
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error,
          onDismissed: () => completer.complete(false),
          action: Button(
            onPressed: () async {
              final result = await download(dll, filePath, silent: silent, force: force);
              completer.complete(result);
            },
            child: Text(translations.downloadDllRetry),
          )
      );
      return completer.future;
    }
  }

  Future<void> downloadAndGuardDependencies() async {
    for(final injectable in GameDll.values) {
      final controller = getDllEditingController(injectable);
      final defaultPath = getDefaultDllPath(injectable);

      if(path.equals(controller.text, defaultPath)) {
        await download(injectable, controller.text);
      }
    }
  }
}

enum UpdateStatus {
  waiting,
  started,
  success,
  error
}