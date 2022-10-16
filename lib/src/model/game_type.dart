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
    return this == GameType.client ? "Client"
        : this == GameType.server ? "Server"
        : "Headless Server";
  }

  String get message {
    return this == GameType.client ? "A fortnite client will be launched to play multiplayer games"
        : this == GameType.server ? "A fortnite client will be launched to host multiplayer games"
        : "A fortnite client will be launched in the background to host multiplayer games";
  }
}