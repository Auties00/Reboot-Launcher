import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';

Future<void> showBrokenError() async {
  showDialog(
      context: appKey.currentContext!,
      builder: (context) => const InfoDialog(
          text: "The lawin server is not working correctly"
      )
  );
}

Future<void> showMissingDllError(String name) async {
  showDialog(
      context: appKey.currentContext!,
      builder: (context) => InfoDialog(
          text: "$name dll is not a valid dll, fix it in the settings tab"
      )
  );
}

Future<void> showTokenError() async {
  showDialog(
      context: appKey.currentContext!,
      builder: (context) => const InfoDialog(
          text: "A token error occurred, restart the game and the lawin server, then try again"
      )
  );
}

Future<void> showUnsupportedHeadless() async {
  showDialog(
      context: appKey.currentContext!,
      builder: (context) => const InfoDialog(
          text: "This version of Fortnite doesn't support headless hosting"
      )
  );
}