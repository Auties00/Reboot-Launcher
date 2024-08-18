import 'dart:io';

import 'package:reboot_common/common.dart';
import 'package:sync/semaphore.dart';

final File launcherLogFile = _createLoggingFile();
final Semaphore _semaphore = Semaphore(1);

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
    await _semaphore.acquire();
    print(message);
    await launcherLogFile.writeAsString("$message\n", mode: FileMode.append, flush: true);
  }catch(error) {
    print("[LOGGER_ERROR] An error occurred while logging: $error");
  }finally {
    _semaphore.release();
  }
}