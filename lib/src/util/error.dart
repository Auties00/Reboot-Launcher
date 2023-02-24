import 'package:fluent_ui/fluent_ui.dart';

import '../../main.dart';
import '../page/home_page.dart';
import '../dialog/dialog.dart';

void onError(Object? exception, StackTrace? stackTrace, bool framework) {
  if(exception == null){
    return;
  }

  if(appKey.currentContext == null || appKey.currentState?.mounted == false){
    return;
  }

  showDialog(
      context: appKey.currentContext!,
      builder: (context) =>
          ErrorDialog(
              exception: exception,
              stackTrace: stackTrace,
              errorMessageBuilder: (exception) => framework ? "An error was thrown by Flutter: $exception" : "An uncaught error was thrown: $exception"
          )
  );
}