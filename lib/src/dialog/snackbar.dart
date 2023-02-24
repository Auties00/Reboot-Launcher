import 'package:fluent_ui/fluent_ui.dart';

import '../../main.dart';
import '../page/home_page.dart';

void showMessage(String text){
  showSnackbar(
      appKey.currentContext!,
      Snackbar(
          content: Text(text, textAlign: TextAlign.center),
          extended: true
      )
  );
}