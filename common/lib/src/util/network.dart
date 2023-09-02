import 'dart:io';

import 'package:reboot_common/common.dart';

bool isLocalHost(String host) => host.trim() == "127.0.0.1"
    || host.trim().toLowerCase() == "localhost"
    || host.trim() == "0.0.0.0";

Future<bool> isPortFree(int port) async {
  try {
    final server = await ServerSocket.bind(InternetAddress.anyIPv4, port);
    await server.close();
    return true;
  } catch (e) {
    return false;
  }
}

Future<void> resetWinNat() async {
  var binary = File("${authenticatorDirectory.path}\\winnat.bat");
  await runElevatedProcess(binary.path, "");
}