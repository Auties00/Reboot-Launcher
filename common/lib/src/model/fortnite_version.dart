import 'dart:io';

class FortniteVersion {
  String name;
  Directory location;

  FortniteVersion.fromJson(json)
      : name = json["name"],
        location = Directory(json["location"]);

  FortniteVersion({required this.name, required this.location});

  Map<String, dynamic> toJson() => {
    'name': name,
    'location': location.path
  };
}