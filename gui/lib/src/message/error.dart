import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:reboot_launcher/src/page/pages.dart';

String? lastError;

void onError(Object exception, StackTrace? stackTrace, bool framework) {
  log("[ERROR_HANDLER] Called");
  log("[ERROR] $exception");
  log("[STACKTRACE] $stackTrace");
  if(pageKey.currentContext == null || pageKey.currentState?.mounted == false){
    log("[ERROR_HANDLER] Not mounted");
    return;
  }

  if(lastError == exception.toString()){
    log("[ERROR_HANDLER] Duplicate");
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