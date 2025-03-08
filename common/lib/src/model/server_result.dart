import 'dart:io';

class ServerResult {
  final ServerResultType type;
  final ServerImplementation? implementation;
  final Object? error;
  final StackTrace? stackTrace;

  ServerResult(this.type, {this.implementation, this.error, this.stackTrace});

  @override
  String toString() {
    return 'ServerResult{type: $type, error: $error, stackTrace: $stackTrace}';
  }
}

class ServerImplementation {
  final Process? process;
  final HttpServer? server;

  ServerImplementation({this.process, this.server});
}

enum ServerResultType {
  starting,
  startMissingHostError,
  startMissingPortError,
  startIllegalPortError,
  startFreeingPort,
  startFreePortSuccess,
  startFreePortError,
  startPingingRemote,
  startPingingLocal,
  startPingError,
  startedImplementation,
  startSuccess,
  startError,
  stopping,
  stopSuccess,
  stopError;

  bool get isStart => name.contains("start");

  bool get isError => name.contains("Error");

  bool get isSuccess => this == ServerResultType.startSuccess || this == ServerResultType.stopSuccess;
}