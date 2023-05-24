import 'package:fluent_ui/fluent_ui.dart';

import '../../../main.dart';
import '../ui/dialog/dialog.dart';

void onError(Object? exception, StackTrace? stackTrace, bool framework) {
  if(exception == null){
    return;
  }

  if(appKey.currentContext == null || appKey.currentState?.mounted == false){
    return;
  }

  WidgetsBinding.instance.addPostFrameCallback((timeStamp) => showDialog(
      context: appKey.currentContext!,
      builder: (context) =>
          ErrorDialog(
              exception: exception,
              stackTrace: stackTrace,
              errorMessageBuilder: (exception) => framework ? "An error was thrown by Flutter: $exception" : "An uncaught error was thrown: $exception"
          )
  ));
}