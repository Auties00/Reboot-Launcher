import 'dart:io';

import 'package:reboot_common/common.dart';
import 'package:synchronized/extension.dart';

final File launcherLogFile = _createLoggingFile();
bool enableLoggingToConsole = true;

File _createLoggingFile() {
  final file = File("${installationDirectory.path}\\launcher.log");
  file.parent.createSync(recursive: true);
  if(file.existsSync()) {
    file.deleteSync();
  }
  file.createSync();
  return file;
}

void log(String message) async {
  try {
    if(enableLoggingToConsole) {
      print(message);
    }

    launcherLogFile.synchronized(() async {
      await launcherLogFile.writeAsString("$message\n", mode: FileMode.append, flush: true);
    });
  }catch(error) {
    print("[LOGGER_ERROR] An error occurred while logging: $error");
  }
}