import 'dart:io';

class GameVersion {
  String name;
  String gameVersion;
  Directory location;

  GameVersion.fromJson(json)
      : name = json["name"],
        gameVersion = json["gameVersion"],
        location = Directory(json["location"]);

  GameVersion({required this.name, required this.gameVersion, required this.location});

  Map<String, dynamic> toJson() => {
    'name': name,
    'gameVersion': gameVersion,
    'location': location.path
  };
}