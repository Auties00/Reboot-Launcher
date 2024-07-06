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

    final lastModified = await _getLastModifiedTime(result);
    if(lastModified != _marker) {
      await Isolate.run(() => patchHeadless(result));
      await _setLastModifiedTime(result);
    }

    return result;
  }

  Future<void> _setLastModifiedTime(File result) async {
    try {
      await result.setLastModified(_marker);
    }catch(_) {
      // Ignored
    }
  }

  Future<DateTime?> _getLastModifiedTime(File result) async {
    try {
      return await result.lastModified();
    }catch(_) {
      return null;
    }
  }

  File? get launcherExecutable => findExecutable(location, "FortniteLauncher.exe");

  File? get eacExecutable => findExecutable(location, "FortniteClient-Win64-Shipping_EAC.exe");

  File? get splashBitmap => findExecutable(location, "Splash.bmp");
}