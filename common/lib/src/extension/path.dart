import 'dart:io';
import 'dart:isolate';
import 'package:path/path.dart' as path;

import 'package:reboot_common/common.dart';

extension FortniteVersionExtension on FortniteVersion {
  static File? findExecutable(Directory directory, String name) {
    try{
      final result = directory.listSync(recursive: true)
          .firstWhere((element) => path.basename(element.path) == name);
      return File(result.path);
    }catch(_){
      return null;
    }
  }

  File? get gameExecutable => findExecutable(location, "FortniteClient-Win64-Shipping.exe");

  Future<File?> get headlessGameExecutable async {
    final result = findExecutable(location, "FortniteClient-Win64-Shipping-Headless.exe");
    if(result != null) {
      return result;
    }

    final original = findExecutable(location, "FortniteClient-Win64-Shipping.exe");
    if(original == null) {
      return null;
    }

    final output = File("${original.parent.path}\\FortniteClient-Win64-Shipping-Headless.exe");
    await original.copy(output.path);
    await Isolate.run(() => patchHeadless(output));
    return output;
  }

  File? get launcherExecutable => findExecutable(location, "FortniteLauncher.exe");

  File? get eacExecutable => findExecutable(location, "FortniteClient-Win64-Shipping_EAC.exe");

  File? get splashBitmap => findExecutable(location, "Splash.bmp");
}