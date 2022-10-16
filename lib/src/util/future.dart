import 'dart:async';

extension FutureExtension<T> on Future<T> {
  bool isCompleted() {
    final completer = Completer<T>();
    then(completer.complete).catchError(completer.completeError);
    return completer.isCompleted;
  }
}