enum ServerType {
  embedded,
  remote,
  local;

  static ServerType? of(String id){
    try {
      return ServerType.values
          .firstWhere((element) => element.id == id);
    }catch(_){
      return null;
    }
  }

  String get id {
    return this == ServerType.embedded ? "embedded"
        : this == ServerType.remote ? "remote"
        : "local";
  }

  String get name {
    return this == ServerType.embedded ? "Embedded (Lawin)"
        : this == ServerType.remote ? "Remote"
        : "Local";
  }

  String get message {
    return this == ServerType.embedded ? "A server will be automatically started in the background"
        : this == ServerType.remote ? "A reverse proxy to the remote server will be created"
        : "Assumes that you are running yourself the server locally";
  }
}