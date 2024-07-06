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

class SettingsController extends GetxController {
  late final GetStorage? _storage;
  late final String originalDll;
  late final TextEditingController gameServerDll;
  late final TextEditingController unrealEngineConsoleDll;
  late final TextEditingController backendDll;
  late final TextEditingController memoryLeakDll;
  late final TextEditingController gameServerPort;
  late final RxString language;
  late final Rx<ThemeMode> themeMode;
  late final RxnInt timestamp;
  late final Rx<UpdateStatus> status;
  late final Rx<UpdateTimer> timer;
  late final TextEditingController url;
  late final RxBool customGameServer;
  late final RxBool firstRun;
  late final Map<String, Future<bool>> _operations;
  late double width;
  late double height;
  late double? offsetX;
  late double? offsetY;
  InfoBarEntry? infoBarEntry;
  Future<bool>? _updater;

  SettingsController() {
    _storage = appWithNoStorage ? null : GetStorage("settings_storage");
    gameServerDll = _createController("game_server", InjectableDll.reboot);
    unrealEngineConsoleDll = _createController("unreal_engine_console", InjectableDll.console);
    backendDll = _createController("backend", InjectableDll.cobalt);
    memoryLeakDll = _createController("memory_leak", InjectableDll.memory);
    gameServerPort = TextEditingController(text: _storage?.read("game_server_port") ?? kDefaultGameServerPort);
    gameServerPort.addListener(() => _storage?.write("game_server_port", gameServerPort.text));
    width = _storage?.read("width") ?? kDefaultWindowWidth;
    height = _storage?.read("height") ?? kDefaultWindowHeight;
    offsetX = _storage?.read("offset_x");
    offsetY = _storage?.read("offset_y");
    themeMode = Rx(ThemeMode.values.elementAt(_storage?.read("theme") ?? 0));
    themeMode.listen((value) => _storage?.write("theme", value.index));
    language = RxString(_storage?.read("language") ?? currentLocale);
    language.listen((value) => _storage?.write("language", value));
    timestamp = RxnInt(_storage?.read("ts"));
    timestamp.listen((value) => _storage?.write("ts", value));
    final timerIndex = _storage?.read("timer");
    timer = Rx(timerIndex == null ? UpdateTimer.hour : UpdateTimer.values.elementAt(timerIndex));
    timer.listen((value) => _storage?.write("timer", value.index));
    url = TextEditingController(text: _storage?.read("update_url") ?? kRebootDownloadUrl);
    url.addListener(() => _storage?.write("update_url", url.text));
    status = Rx(UpdateStatus.waiting);
    customGameServer = RxBool(_storage?.read("custom_game_server") ?? false);
    customGameServer.listen((value) => _storage?.write("custom_game_server", value));
    firstRun = RxBool(_storage?.read("first_run_tutorial") ?? true);
    firstRun.listen((value) => _storage?.write("first_run_tutorial", value));
    _operations = {};
  }

  TextEditingController _createController(String key, InjectableDll dll) {
    final controller = TextEditingController(text: _storage?.read(key) ?? _getDefaultPath(dll));
    controller.addListener(() => _storage?.write(key, controller.text));
    return controller;
  }

  void saveWindowSize(Size size) {
    _storage?.write("width", size.width);
    _storage?.write("height", size.height);
  }

  void saveWindowOffset(Offset position) {
    offsetX = position.dx;
    offsetY = position.dy;
    _storage?.write("offset_x", offsetX);
    _storage?.write("offset_y", offsetY);
  }

  void reset(){
    gameServerDll.text = _getDefaultPath(InjectableDll.reboot);
    unrealEngineConsoleDll.text = _getDefaultPath(InjectableDll.console);
    backendDll.text = _getDefaultPath(InjectableDll.cobalt);
    memoryLeakDll.text = _getDefaultPath(InjectableDll.memory);
    gameServerPort.text = kDefaultGameServerPort;
    timestamp.value = null;
    timer.value = UpdateTimer.never;
    url.text = kRebootDownloadUrl;
    status.value = UpdateStatus.waiting;
    customGameServer.value = false;
    updateReboot();
  }

  Future<void> notifyLauncherUpdate() async {
    if(appVersion == null) {
      return;
    }

    final pubspec = await _getPubspecYaml();
    if(pubspec == null) {
      return;
    }

    final latestVersion = Version.parse(pubspec["version"]);
    if(latestVersion <= appVersion) {
      return;
    }

    late InfoBarEntry infoBar;
    infoBar = showRebootInfoBar(
        translations.updateAvailable(latestVersion.toString()),
        duration: null,
        severity: InfoBarSeverity.warning,
        action: Button(
          child: Text(translations.updateAvailableAction),
          onPressed: () {
            infoBar.close();
            launchUrl(Uri.parse("https://github.com/Auties00/reboot_launcher/releases"));
          },
        )
    );
  }

  Future<dynamic> _getPubspecYaml() async {
    try {
      final pubspecResponse = await http.get(Uri.parse("https://raw.githubusercontent.com/Auties00/reboot_launcher/master/gui/pubspec.yaml"));
      if(pubspecResponse.statusCode != 200) {
        return null;
      }

      return loadYaml(pubspecResponse.body);
    }catch(error) {
      log("[UPDATER] Cannot check for updates: $error");
      return null;
    }
  }

  Future<bool> updateReboot({bool force = false, bool silent = false}) async {
    if(_updater != null) {
      return await _updater!;
    }

    final result = _updateReboot(force, silent);
    _updater = result;
    return await result;
  }

  Future<bool> _updateReboot(bool force, bool silent) async {
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
      timestamp.value = await downloadRebootDll(url.text);
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
      showRebootInfoBar(
          translations.downloadDllError("reboot.dll", error.toString()),
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error,
          action: Button(
            onPressed: () => updateReboot(
                force: true,
                silent: silent
            ),
            child: Text(translations.downloadDllRetry),
          )
      );
      return false;
    }finally {
      _updater = null;
    }
  }

  (File, bool) getInjectableData(InjectableDll dll) {
    final defaultPath = canonicalize(_getDefaultPath(dll));
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

  String _getDefaultPath(InjectableDll dll) => "${dllsDirectory.path}\\${dll.name}.dll";

  Future<bool> downloadCriticalDllInteractive(String filePath, {bool silent = false}) {
    log("[DLL] Asking for $filePath(silent: $silent)");
    final old = _operations[filePath];
    if(old != null) {
      log("[DLL] Download task already exists");
      return old;
    }

    log("[DLL] Creating new download task...");
    final newRun = _downloadCriticalDllInteractive(filePath, silent);
    _operations[filePath] = newRun;
    return newRun;
  }

  Future<bool> _downloadCriticalDllInteractive(String filePath, bool silent) async {
    final fileName = basename(filePath).toLowerCase();
    log("[DLL] File name: $fileName");
    InfoBarEntry? entry;
    try {
      if (fileName == "reboot.dll") {
        log("[DLL] Downloading reboot.dll...");
        return await updateReboot(
            silent: silent
        );
      }

      if(File(filePath).existsSync()) {
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
      error = error.contains(": ") ? error.substring(error.indexOf(": ") + 2) : error;
      error = error.toLowerCase();
      final completer = Completer();
      await showRebootInfoBar(
          translations.downloadDllError(fileName, error.toString()),
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
    }finally {
      _operations.remove(fileName);
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