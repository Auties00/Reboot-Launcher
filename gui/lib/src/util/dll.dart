import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/update_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/util/translations.dart';

final UpdateController _updateController = Get.find<UpdateController>();
Future<void> downloadCriticalDllInteractive(String filePath) async {
  try {
    final fileName = path.basename(filePath);
    if (fileName == "reboot.dll") {
      _updateController.update(true);
      return;
    }

    final fileNameWithoutExtension = path.basenameWithoutExtension(filePath);
    await showInfoBar(
        translations.downloadingDll(fileNameWithoutExtension),
        loading: true,
        duration: null
    );
    await downloadCriticalDll(fileName, filePath);
    await showInfoBar(
        translations.downloadDllSuccess(fileNameWithoutExtension),
        severity: InfoBarSeverity.success,
        duration: infoBarShortDuration
    );
  }catch(message) {
    var error = message.toString();
    error = error.contains(": ") ? error.substring(error.indexOf(": ") + 2) : error;
    error = error.toLowerCase();
    final completer = Completer();
    await showInfoBar(
        translations.downloadDllError(error.toString()),
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
}