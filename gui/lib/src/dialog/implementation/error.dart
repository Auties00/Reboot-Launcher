import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/page/home_page.dart';


String? lastError;

void onError(Object? exception, StackTrace? stackTrace, bool framework) {
  if(exception == null){
    return;
  }

  if(pageKey.currentContext == null || pageKey.currentState?.mounted == false){
    return;
  }

  if(lastError == exception.toString()){
    return;
  }

  lastError = exception.toString();
  var route = ModalRoute.of(pageKey.currentContext!);
  if(route != null && !route.isCurrent){
    Navigator.of(pageKey.currentContext!).pop(false);
  }

  WidgetsBinding.instance.addPostFrameCallback((timeStamp) => showAppDialog(
      builder: (context) =>
          ErrorDialog(
              exception: exception,
              stackTrace: stackTrace,
              errorMessageBuilder: (exception) => framework ? "An error was thrown by Flutter: $exception" : "An uncaught error was thrown: $exception"
          )
  ));
}