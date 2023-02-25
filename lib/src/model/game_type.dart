enum GameType {
  client,
  server,
  headlessServer;

  static GameType? of(String id){
    try {
      return GameType.values
          .firstWhere((element) => element.id == id);
    }catch(_){
      return null;
    }
  }

  String get id {
    return this == GameType.client ? "client"
        : this == GameType.server ? "server"
        : "headless_server";
  }

  String get name {
    return this == GameType.client ? "Game client"
        : this == GameType.server ? "Game server"
        : "Headless game server";
  }
}