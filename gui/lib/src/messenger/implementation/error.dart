import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/messenger/abstract/dialog.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';

String? lastError;

void onError(Object exception, StackTrace? stackTrace, bool framework) {
  log("[ERROR] $exception");
  log("[STACKTRACE] $stackTrace");
  if(pageKey.currentContext == null || pageKey.currentState?.mounted == false){
    return;
  }

  if(lastError == exception.toString()){
    return;
  }

  lastError = exception.toString();
  if(inDialog){
    final context = pageKey.currentContext;
    if(context != null) {
      Navigator.of(context).pop(false);
      inDialog = false;
    }
  }

  WidgetsBinding.instance.addPostFrameCallback((timeStamp) => showRebootDialog(
      builder: (context) =>
          ErrorDialog(
              exception: exception,
              stackTrace: stackTrace,
              errorMessageBuilder: (exception) => translations.uncaughtErrorMessage(exception.toString())
          )
  ));
}