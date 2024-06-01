import 'dart:io';
import 'dart:isolate';

import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';

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

  File? get gameExecutable => findExecutable(location, "FortniteClient-Win64-Shipping.exe");

  Future<File?> get headlessGameExecutable async {
    var result = findExecutable(location, "FortniteClient-Win64-Shipping-Headless.exe");
    if(result != null) {
      return result;
    }

    var original = findExecutable(location, "FortniteClient-Win64-Shipping.exe");
    if(original == null) {
      return null;
    }

    var output = File("${original.parent.path}\\FortniteClient-Win64-Shipping-Headless.exe");
    await original.copy(output.path);
    await Isolate.run(() => patchHeadless(output));
    return output;
  }

  File? get launcherExecutable => findExecutable(location, "FortniteLauncher.exe");

  File? get eacExecutable => findExecutable(location, "FortniteClient-Win64-Shipping_EAC.exe");

  File? get splashBitmap => findExecutable(location, "Splash.bmp");
}