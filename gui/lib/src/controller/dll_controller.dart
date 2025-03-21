import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:path/path.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/messenger/info_bar.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:version/version.dart';
import 'package:path/path.dart' as path;

class DllController extends GetxController {
  static const String storageName = "v2_dll_storage";

  late final GetStorage? _storage;
  late final TextEditingController customGameServerDll;
  late final TextEditingController unrealEngineConsoleDll;
  late final TextEditingController backendDll;
  late final TextEditingController memoryLeakDll;
  late final TextEditingController gameServerPort;
  late final Rx<UpdateTimer> timer;
  late final TextEditingController beforeS20Mirror;
  late final TextEditingController aboveS20Mirror;
  late final RxBool customGameServer;
  late final RxnInt timestamp;
  late final Rx<UpdateStatus> status;

  DllController() {
    _storage = appWithNoStorage ? null : GetStorage(storageName);
    customGameServerDll = _createController("game_server", InjectableDll.gameServer);
    unrealEngineConsoleDll = _createController("unreal_engine_console", InjectableDll.console);
    backendDll = _createController("backend", InjectableDll.auth);
    memoryLeakDll = _createController("memory_leak", InjectableDll.memoryLeak);
    gameServerPort = TextEditingController(text: _storage?.read("game_server_port") ?? kDefaultGameServerPort);
    gameServerPort.addListener(() => _storage?.write("game_server_port", gameServerPort.text));
    final timerIndex = _storage?.read("timer");
    timer = Rx(timerIndex == null ? UpdateTimer.hour : UpdateTimer.values.elementAt(timerIndex));
    timer.listen((value) => _storage?.write("timer", value.index));
    beforeS20Mirror = TextEditingController(text: _storage?.read("update_url") ?? kRebootBelowS20DownloadUrl);
    beforeS20Mirror.addListener(() => _storage?.write("update_url", beforeS20Mirror.text));
    aboveS20Mirror = TextEditingController(text: _storage?.read("old_update_url") ?? kRebootAboveS20DownloadUrl);
    aboveS20Mirror.addListener(() => _storage?.write("new_update_url", aboveS20Mirror.text));
    status = Rx(UpdateStatus.waiting);
    customGameServer = RxBool(_storage?.read("custom_game_server") ?? false);
    customGameServer.listen((value) => _storage?.write("custom_game_server", value));
    timestamp = RxnInt(_storage?.read("ts"));
    timestamp.listen((value) => _storage?.write("ts", value));
  }

  TextEditingController _createController(String key, InjectableDll dll) {
    final controller = TextEditingController(text: _storage?.read(key) ?? getDefaultDllPath(dll));
    controller.addListener(() => _storage?.write(key, controller.text));
    return controller;
  }

  void resetGame() {
    customGameServerDll.text = getDefaultDllPath(InjectableDll.gameServer);
    unrealEngineConsoleDll.text = getDefaultDllPath(InjectableDll.console);
    backendDll.text = getDefaultDllPath(InjectableDll.auth);
  }

  void resetServer() {
    gameServerPort.text = kDefaultGameServerPort;
    timer.value = UpdateTimer.hour;
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
          hours: timer.value.hours,
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
      await Future.wait(
          [
            downloadRebootDll(rebootBeforeS20DllFile, beforeS20Mirror.text, false),
            downloadRebootDll(rebootAboveS20DllFile, aboveS20Mirror.text, true),
            Future.delayed(const Duration(seconds: 1))
          ],
          eagerError: false
      );
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
      infoBarEntry = showRebootInfoBar(
          translations.downloadDllError(error.toString(), "reboot.dll"),
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error,
          action: Button(
            onPressed: () async {
              infoBarEntry?.close();
              updateGameServerDll(
                  force: true,
                  silent: silent
              );
            },
            child: Text(translations.downloadDllRetry),
          )
      );
      return false;
    }
  }

  (File, bool) getInjectableData(Version version, InjectableDll dll) {
    final defaultPath = canonicalize(getDefaultDllPath(dll));
    switch(dll){
      case InjectableDll.gameServer:
        if(customGameServer.value) {
          return (File(customGameServerDll.text), true);
        }

        return (version.major >= 20 ? rebootAboveS20DllFile : rebootBeforeS20DllFile, false);
      case InjectableDll.console:
        final ue4ConsoleFile = File(unrealEngineConsoleDll.text);
        return (ue4ConsoleFile, canonicalize(ue4ConsoleFile.path) != defaultPath);
      case InjectableDll.auth:
        final backendFile = File(backendDll.text);
        return (backendFile, canonicalize(backendFile.path) != defaultPath);
      case InjectableDll.memoryLeak:
        final memoryFile = File(memoryLeakDll.text);
        return (memoryFile, canonicalize(memoryFile.path) != defaultPath);
    }
  }

  TextEditingController getDllEditingController(InjectableDll dll) {
    switch(dll) {
      case InjectableDll.console:
        return unrealEngineConsoleDll;
      case InjectableDll.auth:
        return backendDll;
      case InjectableDll.gameServer:
        return customGameServerDll;
      case InjectableDll.memoryLeak:
        return memoryLeakDll;
    }
  }

  String getDefaultDllPath(InjectableDll dll) {
   switch(dll) {
     case InjectableDll.console:
       return "${dllsDirectory.path}\\console.dll";
     case InjectableDll.auth:
       return "${dllsDirectory.path}\\cobalt.dll";
     case InjectableDll.gameServer:
       return "${dllsDirectory.path}\\reboot.dll";
     case InjectableDll.memoryLeak:
       return "${dllsDirectory.path}\\memory.dll";
   }
  }

  Future<bool> download(InjectableDll dll, String filePath, {bool silent = false, bool force = false}) async {
    log("[DLL] Asking for $dll at $filePath(silent: $silent, force: $force)");
    InfoBarEntry? entry;
    try {
      if (dll == InjectableDll.gameServer) {
        return await updateGameServerDll(silent: silent);
      }

      if(!force && File(filePath).existsSync()) {
        log("[DLL] File already exists");
        return true;
      }

      final fileNameWithoutExtension = basenameWithoutExtension(filePath);
      if(!silent) {
        entry = showRebootInfoBar(
            translations.downloadingDll(fileNameWithoutExtension),
            loading: true,
            duration: null
        );
      }
      await downloadDependency(dll, filePath);
      entry?.close();
      if(!silent) {
        entry = await showRebootInfoBar(
            translations.downloadDllSuccess(fileNameWithoutExtension),
            severity: InfoBarSeverity.success,
            duration: infoBarShortDuration
        );
      }
      return true;
    }catch(message) {
      log("[DLL] Error: $message");
      entry?.close();
      var error = message.toString();
      error =
      error.contains(": ") ? error.substring(error.indexOf(": ") + 2) : error;
      error = error.toLowerCase();
      final completer = Completer();
      await showRebootInfoBar(
          translations.downloadDllError(error.toString(), dll.name),
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error,
          onDismissed: () => completer.complete(null),
          action: Button(
            onPressed: () async {
              await download(dll, filePath, silent: silent, force: force);
              completer.complete(null);
            },
            child: Text(translations.downloadDllRetry),
          )
      );
      await completer.future;
      return false;
    }
  }

  void guardFiles() {
    for(final injectable in InjectableDll.values) {
      final controller = getDllEditingController(injectable);
      final defaultPath = getDefaultDllPath(injectable);
      if (path.equals(controller.text, defaultPath)) {
        download(injectable, controller.text);
      }
      controller.addListener(() async {
        try {
          if (!path.equals(controller.text, defaultPath)) {
            return;
          }

          final filePath = controller.text;
          await for(final event in File(filePath).parent.watch(events: FileSystemEvent.delete | FileSystemEvent.move)) {
            if (path.equals(event.path, filePath)) {
              await download(injectable, filePath);
            }
          }
        } catch(_) {
          // Ignore
        }
      });
    }
  }
}

extension _UpdateTimerExtension on UpdateTimer {
  int get hours {
    switch(this) {
      case UpdateTimer.never:
        return -1;
      case UpdateTimer.hour:
        return 1;
      case UpdateTimer.day:
        return 24;
      case UpdateTimer.week:
        return 24 * 7;
    }
  }
}