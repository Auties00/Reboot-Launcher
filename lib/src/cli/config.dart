import 'dart:convert';

import 'package:args/args.dart';

import '../model/fortnite_version.dart';
import '../model/game_type.dart';
import '../model/server_type.dart';

Iterable<String> getGameTypes() => GameType.values.map((entry) => entry.id);

Iterable<String> getServerTypes() => ServerType.values.map((entry) => entry.id);

String getDefaultServerType(Map<String, dynamic> json) {
  var type = ServerType.values.elementAt(json["type"] ?? 0);
  return type.id;
}

GameType getGameType(ArgResults result) {
  var type = GameType.of(result["type"]);
  if(type == null){
    throw Exception("Unknown game type: $result. Use --type only with ${getGameTypes().join(", ")}");
  }

  return type;
}

ServerType getServerType(ArgResults result) {
  var type = ServerType.of(result["server-type"]);
  if(type == null){
    throw Exception("Unknown server type: $result. Use --server-type only with ${getServerTypes().join(", ")}");
  }

  return type;
}

String getDefaultGameType(Map<String, dynamic> json){
  var type = GameType.values.elementAt(json["type"] ?? 0);
  switch(type){
    case GameType.client:
      return "client";
    case GameType.server:
      return "server";
    case GameType.headlessServer:
      return "headless_server";
  }
}

List<FortniteVersion> getVersions(Map<String, dynamic> gameJson) {
  Iterable iterable = jsonDecode(gameJson["versions"] ?? "[]");
  return iterable.map((entry) => FortniteVersion.fromJson(entry))
      .toList();
}