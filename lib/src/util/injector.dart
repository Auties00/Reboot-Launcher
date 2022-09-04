import 'dart:io';

import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/util/locate_binary.dart';

File injectLogFile = File("${Platform.environment["Temp"]}/server.txt");

// This can be done easily with win32 apis but for some reason it doesn't work on all machines
Future<bool> injectDll(int pid, String dll) async {
  var shell = Shell(workingDirectory: binariesDirectory);
  var process = await shell.run("./injector.exe -p $pid --inject \"$dll\"");
  var success = process.outText.contains("Successfully injected module");
  if (!success) {
    injectLogFile.writeAsString(process.outText, mode: FileMode.append);
  }

  return success;
}
