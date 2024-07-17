import 'dart:io';
import 'dart:isolate';

import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';

extension FortniteVersionExtension on FortniteVersion {
  static String _marker = "FortniteClient.mod";

  static File? findFile(Directory directory, String name) {
    try{
      final result = directory.listSync(recursive: true)
          .firstWhere((element) => path.basename(element.path) == name);
      return File(result.path);
    }catch(_){
      return null;
    }
  }

  Future<File?> get shippingExecutable async {
    final result = findFile(location, "FortniteClient-Win64-Shipping.exe");
    if(result == null) {
      return null;
    }

    final marker = findFile(location, _marker);
    if(marker != null) {
      return result;
    }

    await Isolate.run(() => patchHeadless(result));
    await File("${location.path}\\$_marker").create();
    return result;
  }

  File? get launcherExecutable => findFile(location, "FortniteLauncher.exe");

  File? get eacExecutable => findFile(location, "FortniteClient-Win64-Shipping_EAC.exe");

  File? get splashBitmap => findFile(location, "Splash.bmp");
}