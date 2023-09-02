class ServerResult {
  final ServerResultType type;
  final Object? error;
  final StackTrace? stackTrace;

  ServerResult(this.type, {this.error, this.stackTrace});
}

enum ServerResultType {
  missingHostError,
  missingPortError,
  illegalPortError,
  freeingPort,
  freePortSuccess,
  freePortError,
  pingingRemote,
  pingingLocal,
  pingError,
  startSuccess,
  startError;

  bool get isError => name.contains("Error");
}