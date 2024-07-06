import 'dart:io';

import 'package:version/version.dart';

class FortniteVersion {
  Version content;
  Directory location;

  FortniteVersion.fromJson(json)
      : content = Version.parse(json["content"]),
        location = Directory(json["location"]);

  FortniteVersion({required this.content, required this.location});

  Map<String, dynamic> toJson() => {
    'content': content.toString(),
    'location': location.path
  };

  @override
  bool operator ==(Object other) => other is FortniteVersion && this.content == other.content;
}