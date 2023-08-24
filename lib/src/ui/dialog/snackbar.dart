import 'package:fluent_ui/fluent_ui.dart';

import 'package:reboot_launcher/main.dart';

void showMessage(String text){
  showSnackbar(
      appKey.currentContext!,
      Snackbar(
          content: Text(text, textAlign: TextAlign.center),
          extended: true
      )
  );
}