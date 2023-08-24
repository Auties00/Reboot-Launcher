import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_launcher/src/util/patcher.dart';

class FortniteVersion {
  String name;
  Directory location;

  FortniteVersion.fromJson(json)
      : name = json["name"],
        location = Directory(json["location"]);

  FortniteVersion({required this.name, required this.location});

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

    await Future.wait([
      compute(patchMatchmaking, original),
      compute(patchHeadless, original)
    ]);
    return original;
  }

  File? get launcher {
    return findExecutable(location, "FortniteLauncher.exe");
  }

  File? get eacExecutable {
    return findExecutable(location, "FortniteClient-Win64-Shipping_EAC.exe");
  }

  Map<String, dynamic> toJson() => {
    'name': name,
    'location': location.path
  };

  @override
  String toString() {
    return 'FortniteVersion{name: $name, location: $location';
  }
}
