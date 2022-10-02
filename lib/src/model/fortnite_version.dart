import 'dart:io';

import 'package:get/get.dart';
import 'package:path/path.dart' as path;

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
          .firstWhereOrNull((element) => path.basename(element.path) == name);
      if(result == null){
        return null;
      }

      return File(result.path);
    }catch(_){
      return null;
    }
  }

  File? get executable {
    return findExecutable(location, "FortniteClient-Win64-Shipping.exe");
  }

  File? get launcher {
    return findExecutable(location, "FortniteLauncher.exe");
  }

  File? get eacExecutable {
    return findExecutable(location, "FortniteClient-Win64-Shipping_EAC.exe");
  }

  Map<String, dynamic> toJson() => {
        'name': name,
        'location': location.path,
      };

  @override
  String toString() {
    return 'FortniteVersion{name: $name, location: $location}';
  }
}
