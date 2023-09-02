import 'dart:io';
import 'dart:isolate';

import 'package:reboot_common/common.dart';
import 'package:path/path.dart' as path;

Directory get installationDirectory =>
    File(Platform.resolvedExecutable).parent;

Directory get assetsDirectory {
  var directory = Directory("${installationDirectory.path}\\data\\flutter_assets\\assets");
  if(directory.existsSync()) {
    return directory;
  }

  return installationDirectory;
}

Directory get logsDirectory =>
    Directory("${installationDirectory.path}\\logs");

Directory get settingsDirectory =>
    Directory("${installationDirectory.path}\\settings");

Directory get tempDirectory =>
    Directory(Platform.environment["Temp"]!);

Future<bool> delete(FileSystemEntity file) async {
  try {
    await file.delete(recursive: true);
    return true;
  }catch(_){
    return Future.delayed(const Duration(seconds: 5)).then((value) async {
      try {
        await file.delete(recursive: true);
        return true;
      }catch(_){
        return false;
      }
    });
  }
}

extension FortniteVersionExtension on FortniteVersion {
  static File? findExecutable(Directory directory, String name) {
    try{
      var result = directory.listSync(recursive: true)
          .firstWhere((element) => path.basename(element.path) == name);
      return File(result.path);
    }catch(_){
      return null;
    }
  }

  Future<File?> get executable async {
    var result = findExecutable(location, "FortniteClient-Win64-Shipping-Reboot.exe");
    if(result != null) {
      return result;
    }

    var original = findExecutable(location, "FortniteClient-Win64-Shipping.exe");
    if(original == null) {
      return null;
    }

    var output = File("${original.parent.path}\\FortniteClient-Win64-Shipping-Reboot.exe");
    await original.copy(output.path);
    await Future.wait([
      Isolate.run(() => patchMatchmaking(output)),
      Isolate.run(() => patchHeadless(output)),
    ]);
    return output;
  }

  File? get launcher => findExecutable(location, "FortniteLauncher.exe");

  File? get eacExecutable => findExecutable(location, "FortniteClient-Win64-Shipping_EAC.exe");

  File? get splashBitmap => findExecutable(location, "Splash.bmp");
}