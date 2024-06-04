import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';

import '../../util/log.dart';


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
  var route = ModalRoute.of(pageKey.currentContext!);
  if(route != null && !route.isCurrent){
    Navigator.of(pageKey.currentContext!).pop(false);
  }

  WidgetsBinding.instance.addPostFrameCallback((timeStamp) => showAppDialog(
      builder: (context) =>
          ErrorDialog(
              exception: exception,
              stackTrace: stackTrace,
              errorMessageBuilder: (exception) => translations.uncaughtErrorMessage(exception.toString())
          )
  ));
}