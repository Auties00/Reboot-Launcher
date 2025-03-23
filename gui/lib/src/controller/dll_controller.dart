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
import 'package:reboot_launcher/src/widget/page/settings_page.dart';
import 'package:version/version.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';

class DllController extends GetxController {
  static const String storageName = "v3_dll_storage";

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
  late final Map<InjectableDll, StreamSubscription?> _subscriptions;

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
    beforeS20Mirror = TextEditingController(text: _storage?.read("before_s20_update_url") ?? kRebootBelowS20DownloadUrl);
    beforeS20Mirror.addListener(() => _storage?.write("before_s20_update_url", beforeS20Mirror.text));
    aboveS20Mirror = TextEditingController(text: _storage?.read("after_s20_update_url") ?? kRebootAboveS20DownloadUrl);
    aboveS20Mirror.addListener(() => _storage?.write("after_s20_update_url", aboveS20Mirror.text));
    status = Rx(UpdateStatus.waiting);
    customGameServer = RxBool(_storage?.read("custom_game_server") ?? false);
    customGameServer.listen((value) => _storage?.write("custom_game_server", value));
    timestamp = RxnInt(_storage?.read("ts"));
    timestamp.listen((value) => _storage?.write("ts", value));
    _subscriptions = {};
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
        _listenToFileEvents(InjectableDll.gameServer);
        return true;
      }

      final needsUpdate = await hasRebootDllUpdate(
          timestamp.value,
          hours: timer.value.hours,
          force: force
      );
      if(!needsUpdate) {
        status.value = UpdateStatus.success;
        _listenToFileEvents(InjectableDll.gameServer);
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
      _listenToFileEvents(InjectableDll.gameServer);
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

  (File, bool) getInjectableData(String version, InjectableDll dll) {
    final defaultPath = canonicalize(getDefaultDllPath(dll));
    switch(dll){
      case InjectableDll.gameServer:
        if(customGameServer.value) {
          return (File(customGameServerDll.text), true);
        }

        return (_isS20(version) ? rebootAboveS20DllFile : rebootBeforeS20DllFile, false);
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

  bool _isS20(String version) {
    try {
      return Version.parse(version).major >= 20;
    } on FormatException catch(_) {
      return version.trim().startsWith("20.");
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
        log("[DLL] $dll already exists");
        _listenToFileEvents(dll);
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
      _listenToFileEvents(dll);
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
    for(final injectable in InjectableDll.values) {
      final controller = getDllEditingController(injectable);
      final defaultPath = getDefaultDllPath(injectable);

      if(path.equals(controller.text, defaultPath)) {
        await download(injectable, controller.text);
      }
    }
  }

  void _listenToFileEvents(InjectableDll injectable) {
    final controller = getDllEditingController(injectable);
    final defaultPath = getDefaultDllPath(injectable);

    void onFileEvent(FileSystemEvent event, String filePath) {
      if (!path.equals(event.path, filePath)) {
        return;
      }

      if(path.equals(filePath, defaultPath)) {
        Get.find<GameController>()
            .instance
            .value
            ?.kill();
        Get.find<HostingController>()
            .instance
            .value
            ?.kill();
        showRebootInfoBar(
            translations.downloadDllAntivirus(antiVirusName ?? defaultAntiVirusName, injectable.name),
            duration: infoBarLongDuration,
            severity: InfoBarSeverity.error
        );
      }

      _updateInput(injectable);
    }

    StreamSubscription subscribe(String filePath) => File(filePath)
        .parent
        .watch(events: FileSystemEvent.delete | FileSystemEvent.move)
        .listen((event) => onFileEvent(event, filePath));

    controller.addListener(() {
      _subscriptions[injectable]?.cancel();
      _subscriptions[injectable] = subscribe(controller.text);
    });
    _subscriptions[injectable] = subscribe(controller.text);
  }

  void _updateInput(InjectableDll injectable) {
     switch(injectable) {
      case InjectableDll.console:
        settingsConsoleDllInputKey.currentState?.validate();
        break;
      case InjectableDll.auth:
        settingsAuthDllInputKey.currentState?.validate();
        break;
      case InjectableDll.gameServer:
        settingsGameServerDllInputKey.currentState?.validate();
        break;
      case InjectableDll.memoryLeak:
        settingsMemoryDllInputKey.currentState?.validate();
        break;
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