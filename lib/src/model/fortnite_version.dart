import 'dart:io';
import 'package:path/path.dart' as path;

class FortniteVersion {
  String name;
  Directory location;

  FortniteVersion.fromJson(json)
      : name = json["name"],
        location = Directory(json["location"]);

  FortniteVersion({required this.name, required this.location});

  static File findExecutable(Directory directory, String name) {
    var home = path.basename(directory.path) == "FortniteGame"
        ? directory
        : directory.listSync(recursive: true).firstWhere(
            (element) => path.basename(element.path) == "FortniteGame");
    return File("${home.path}/Binaries/Win64/$name");
  }

  File get executable {
    return findExecutable(location, "FortniteClient-Win64-Shipping.exe");
  }

  File get launcher {
    return findExecutable(location, "FortniteLauncher.exe");
  }

  File get eacExecutable {
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
