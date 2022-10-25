import 'dart:io';

import 'package:path/path.dart' as path;

class FortniteVersion {
  String name;
  Directory location;
  bool memoryFix;

  FortniteVersion.fromJson(json)
      : name = json["name"],
        location = Directory(json["location"]),
        memoryFix = json["memory_fix"] ?? false;

  FortniteVersion({required this.name, required this.location, required this.memoryFix});

  static File? findExecutable(Directory directory, String name) {
    try{
      var result = directory.listSync(recursive: true)
          .firstWhere((element) => path.basename(element.path) == name);
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

  Map<String, dynamic> toJson() => {
        'name': name,
        'location': location.path,
      };

  @override
  String toString() {
    return 'FortniteVersion{name: $name, location: $location}';
  }
}
