import 'dart:io';

import 'package:reboot_common/common.dart';

final File _script = File("${assetsDirectory.path}\\misc\\udp.ps1");

Future<bool> pingGameServer(String address, {Duration? timeout}) async {
  var start = DateTime.now();
  var firstTime = true;
  while (firstTime || (timeout != null && DateTime.now().millisecondsSinceEpoch - start.millisecondsSinceEpoch < timeout.inMilliseconds)) {
    var split = address.split(":");
    var hostname = split[0];
    var port = split.length > 1 ? split[1] : kDefaultGameServerPort;
    var result = await Process.run(
        "powershell",
        [
          _script.path,
          hostname,
          port
        ]
    );
    if (result.exitCode == 0) {
      return true;
    }

    if(firstTime) {
      firstTime = false;
    }else {
      await Future.delayed(const Duration(seconds: 2));
    }
  }

  return false;
}