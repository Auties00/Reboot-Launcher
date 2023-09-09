class ServerResult {
  final ServerResultType type;
  final Object? error;
  final StackTrace? stackTrace;

  ServerResult(this.type, {this.error, this.stackTrace});
}

enum ServerResultType {
  starting,
  startSuccess,
  startError,
  stopping,
  stopSuccess,
  stopError,
  missingHostError,
  missingPortError,
  illegalPortError,
  freeingPort,
  freePortSuccess,
  freePortError,
  pingingRemote,
  pingingLocal,
  pingError;

  bool get isError => name.contains("Error");
}