import 'dart:io';

class AuthBackendResult {
  final AuthBackendResultType type;
  final AuthBackendImplementation? implementation;
  final Object? error;
  final StackTrace? stackTrace;

  AuthBackendResult(this.type, {this.implementation, this.error, this.stackTrace});

  @override
  String toString() {
    return 'ServerResult{type: $type, error: $error, stackTrace: $stackTrace}';
  }
}

class AuthBackendImplementation {
  final Process? process;
  final HttpServer? server;

  AuthBackendImplementation({this.process, this.server});
}

enum AuthBackendResultType {
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

  bool get isStart => name.startsWith("start");

  bool get isError => name.endsWith("Error");

  bool get isSuccess => this == AuthBackendResultType.startSuccess || this == AuthBackendResultType.stopSuccess;
}