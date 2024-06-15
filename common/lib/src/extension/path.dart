import 'dart:io';
import 'dart:isolate';

import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';

extension FortniteVersionExtension on FortniteVersion {
  static DateTime _marker = DateTime.fromMicrosecondsSinceEpoch(0);

  static File? findExecutable(Directory directory, String name) {
    try{
      final result = directory.listSync(recursive: true)
          .firstWhere((element) => path.basename(element.path) == name);
      return File(result.path);
    }catch(_){
      return null;
    }
  }

  Future<File?> get shippingExecutable async {
    final result = findExecutable(location, "FortniteClient-Win64-Shipping.exe");
    if(result == null) {
      return null;
    }

    final lastModified = await result.lastModified();
    if(lastModified != _marker) {
      print("Applying patch");
      await Isolate.run(() => patchHeadless(result));
      await result.setLastModified(_marker);
    }

    return result;
  }

  File? get launcherExecutable => findExecutable(location, "FortniteLauncher.exe");

  File? get eacExecutable => findExecutable(location, "FortniteClient-Win64-Shipping_EAC.exe");

  File? get splashBitmap => findExecutable(location, "Splash.bmp");
}