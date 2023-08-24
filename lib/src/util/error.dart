import 'package:fluent_ui/fluent_ui.dart';

import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/ui/dialog/dialog.dart';


String? lastError;

void onError(Object? exception, StackTrace? stackTrace, bool framework) {
  if(exception == null){
    return;
  }

  if(appKey.currentContext == null || appKey.currentState?.mounted == false){
    return;
  }

  if(lastError == exception.toString()){
    return;
  }

  lastError = exception.toString();
  var route = ModalRoute.of(appKey.currentContext!);
  if(route != null && !route.isCurrent){
    Navigator.of(appKey.currentContext!).pop(false);
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