import 'dart:io';

import 'package:reboot_common/common.dart';
import 'package:sync/semaphore.dart';

final File _loggingFile = _createLoggingFile();
final Semaphore _semaphore = Semaphore(1);

File _createLoggingFile() {
  final file = File("${logsDirectory.path}\\launcher.log");
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
    await _loggingFile.writeAsString("$message\n", mode: FileMode.append, flush: true);
  }catch(error) {
    print(error);
  }finally {
    _semaphore.release();
  }
}