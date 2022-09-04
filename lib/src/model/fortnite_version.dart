import 'dart:io';

class FortniteVersion {
  String name;
  Directory location;

  FortniteVersion.fromJson(json)
      : name = json["name"],
        location = Directory(json["location"]);

  FortniteVersion({required this.name, required this.location});

  static File findExecutable(Directory directory, String name) {
    return File(
        "${directory.path}/FortniteGame/Binaries/Win64/$name");
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
