import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/controller/update_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/util/translations.dart';

final UpdateController _updateController = Get.find<UpdateController>();
final Map<String, Future<void>> _operations = {};

Future<void> downloadCriticalDllInteractive(String filePath, {bool silent = false}) {
  final old = _operations[filePath];
  if(old != null) {
    return old;
  }

  final newRun = _downloadCriticalDllInteractive(filePath, silent);
  _operations[filePath] = newRun;
  return newRun;
}

Future<void> _downloadCriticalDllInteractive(String filePath, bool silent) async {
  final fileName = path.basename(filePath).toLowerCase();
  InfoBarEntry? entry;
  try {
    if (fileName == "reboot.dll") {
      await _updateController.updateReboot(
        silent: silent
      );
      return;
    }

    if(File(filePath).existsSync()) {
      return;
    }

    final fileNameWithoutExtension = path.basenameWithoutExtension(filePath);
    if(!silent) {
      entry = showInfoBar(
          translations.downloadingDll(fileNameWithoutExtension),
          loading: true,
          duration: null
      );
    }
    await downloadCriticalDll(fileName, filePath);
    entry?.close();
    if(!silent) {
      entry = await showInfoBar(
          translations.downloadDllSuccess(fileNameWithoutExtension),
          severity: InfoBarSeverity.success,
          duration: infoBarShortDuration
      );
    }
  }catch(message) {
    if(!silent) {
      entry?.close();
      var error = message.toString();
      error = error.contains(": ") ? error.substring(error.indexOf(": ") + 2) : error;
      error = error.toLowerCase();
      final completer = Completer();
      await showInfoBar(
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
    }
  }finally {
    _operations.remove(fileName);
  }
}

extension InjectableDllExtension on InjectableDll {
  String get path {
    final SettingsController settingsController = Get.find<SettingsController>();
    switch(this){
      case InjectableDll.reboot:
        if(_updateController.customGameServer.value) {
          final file = File(settingsController.gameServerDll.text);
          if(file.existsSync()) {
            return file.path;
          }
        }

        return rebootDllFile.path;
      case InjectableDll.console:
        return settingsController.unrealEngineConsoleDll.text;
      case InjectableDll.cobalt:
        return settingsController.backendDll.text;
      case InjectableDll.memory:
        return settingsController.memoryLeakDll.text;
    }
  }
}
