import 'dart:io';

class FortniteVersion {
  String name;
  String gameVersion;
  Directory location;

  FortniteVersion.fromJson(json)
      : name = json["name"],
        gameVersion = json["gameVersion"],
        location = Directory(json["location"]);

  FortniteVersion({required this.name, required this.gameVersion, required this.location});

  Map<String, dynamic> toJson() => {
    'name': name,
    'gameVersion': gameVersion,
    'location': location.path
  };
}