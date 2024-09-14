import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/messenger/abstract/info_bar.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:version/version.dart';
import 'package:yaml/yaml.dart';

class DllController extends GetxController {
  static const String storageName = "dll_storage";

  late final GetStorage? _storage;
  late final String originalDll;
  late final TextEditingController gameServerDll;
  late final TextEditingController unrealEngineConsoleDll;
  late final TextEditingController backendDll;
  late final TextEditingController memoryLeakDll;
  late final TextEditingController gameServerPort;
  late final Rx<UpdateTimer> timer;
  late final TextEditingController url;
  late final RxBool customGameServer;
  late final RxnInt timestamp;
  late final Rx<UpdateStatus> status;
  InfoBarEntry? infoBarEntry;
  Future<bool>? _updater;

  DllController() {
    _storage = appWithNoStorage ? null : GetStorage(storageName);
    gameServerDll = _createController("game_server", InjectableDll.reboot);
    unrealEngineConsoleDll = _createController("unreal_engine_console", InjectableDll.console);
    backendDll = _createController("backend", InjectableDll.cobalt);
    memoryLeakDll = _createController("memory_leak", InjectableDll.memory);
    gameServerPort = TextEditingController(text: _storage?.read("game_server_port") ?? kDefaultGameServerPort);
    gameServerPort.addListener(() => _storage?.write("game_server_port", gameServerPort.text));
    final timerIndex = _storage?.read("timer");
    timer = Rx(timerIndex == null ? UpdateTimer.hour : UpdateTimer.values.elementAt(timerIndex));
    timer.listen((value) => _storage?.write("timer", value.index));
    url = TextEditingController(text: _storage?.read("update_url") ?? kRebootDownloadUrl);
    url.addListener(() => _storage?.write("update_url", url.text));
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
    gameServerDll.text = getDefaultDllPath(InjectableDll.reboot);
    unrealEngineConsoleDll.text = getDefaultDllPath(InjectableDll.console);
    backendDll.text = getDefaultDllPath(InjectableDll.cobalt);
    memoryLeakDll.text = getDefaultDllPath(InjectableDll.memory);
  }

  void resetServer() {
    gameServerPort.text = kDefaultGameServerPort;
    timer.value = UpdateTimer.hour;
    url.text = kRebootDownloadUrl;
    status.value = UpdateStatus.waiting;
    customGameServer.value = false;
    timestamp.value = null;
    updateGameServerDll();
  }

  Future<bool> updateGameServerDll({bool force = false, bool silent = false}) async {
    if(_updater != null) {
      return await _updater!;
    }

    final result = _updateGameServerDll(force, silent);
    _updater = result;
    return await result;
  }

  Future<bool> _updateGameServerDll(bool force, bool silent) async {
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
      final result = downloadRebootDll(url.text);
      timestamp.value = await Future.wait([result, Future.delayed(const Duration(seconds: 1))], eagerError: false)
          .then((_) => result);
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
    }finally {
      _updater = null;
    }
  }

  (File, bool) getInjectableData(InjectableDll dll) {
    final defaultPath = canonicalize(getDefaultDllPath(dll));
    switch(dll){
      case InjectableDll.reboot:
        if(customGameServer.value) {
          final file = File(gameServerDll.text);
          if(file.existsSync()) {
            return (file, true);
          }
        }

        return (rebootDllFile, false);
      case InjectableDll.console:
        final ue4ConsoleFile = File(unrealEngineConsoleDll.text);
        return (ue4ConsoleFile, canonicalize(ue4ConsoleFile.path) != defaultPath);
      case InjectableDll.cobalt:
        final backendFile = File(backendDll.text);
        return (backendFile, canonicalize(backendFile.path) != defaultPath);
      case InjectableDll.memory:
        final memoryLeakFile = File(memoryLeakDll.text);
        return (memoryLeakFile, canonicalize(memoryLeakFile.path) != defaultPath);
    }
  }

  String getDefaultDllPath(InjectableDll dll) => "${dllsDirectory.path}\\${dll.name}.dll";

  Future<bool> downloadCriticalDllInteractive(String filePath, {bool silent = false, bool force = false}) async {
    log("[DLL] Asking for $filePath(silent: $silent)");
    final fileName = basename(filePath).toLowerCase();
    log("[DLL] File name: $fileName");
    InfoBarEntry? entry;
    try {
      if (fileName == "reboot.dll") {
        log("[DLL] Downloading reboot.dll...");
        return await updateGameServerDll(
            silent: silent
        );
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
      await downloadCriticalDll(fileName, filePath);
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
          translations.downloadDllError(error.toString(), fileName),
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error,
          onDismissed: () => completer.complete(null),
          action: Button(
            onPressed: () async {
              await downloadCriticalDllInteractive(filePath);
              completer.complete(null);
            },
            child: Text(translations.downloadDllRetry),
          )
      );
      await completer.future;
      return false;
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