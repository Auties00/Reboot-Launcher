import 'dart:convert';

import 'package:args/args.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/model/server_type.dart';

Iterable<String> getServerTypes() => ServerType.values.map((entry) => entry.id);

String getDefaultServerType(Map<String, dynamic> json) {
  var type = ServerType.values.elementAt(json["type"] ?? 0);
  return type.id;
}

ServerType getServerType(ArgResults result) {
  var type = ServerType.of(result["server-type"]);
  if(type == null){
    throw Exception("Unknown server type: $result. Use --server-type only with ${getServerTypes().join(", ")}");
  }

  return type;
}

List<FortniteVersion> getVersions(Map<String, dynamic> gameJson) {
  Iterable iterable = jsonDecode(gameJson["versions"] ?? "[]");
  return iterable.map((entry) => FortniteVersion.fromJson(entry))
      .toList();
}