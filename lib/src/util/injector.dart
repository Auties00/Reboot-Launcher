import 'dart:io';

import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/util/binary.dart';

File injectLogFile = File("${Platform.environment["Temp"]}/server.txt");

// This can be done easily with win32 apis but for some reason it doesn't work on all machines
// Update: it was a missing permission error, it could be refactored now
Future<bool> injectDll(int pid, String dll) async {
  var shell = Shell(workingDirectory: internalBinariesDirectory);
  var process = await shell.run("./injector.exe -p $pid --inject \"$dll\"");
  var success = process.outText.contains("Successfully injected module");
  if (!success) {
    injectLogFile.writeAsString(process.outText);
  }

  return success;
}
