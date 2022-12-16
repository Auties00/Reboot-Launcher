import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';

Future<void> showBrokenError() async {
  showDialog(
      context: appKey.currentContext!,
      builder: (context) => const InfoDialog(
          text: "The backend server is not working correctly"
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

Future<void> showTokenErrorFixable() async {
  showDialog(
      context: appKey.currentContext!,
      builder: (context) => const InfoDialog(
          text: "A token error occurred. "
              "The backend server has been automatically restarted to fix the issue. "
              "Relaunch your game to check if the issue has been automatically fixed. "
              "Otherwise, open an issue on Discord."
      )
  );
}

Future<void> showTokenErrorUnfixable() async {
  showDialog(
      context: appKey.currentContext!,
      builder: (context) => const InfoDialog(
          text: "A token error occurred. "
              "This issue cannot be resolved automatically as the server isn't embedded."
              "Please restart the server manually, then relaunch your game to check if the issue has been fixed. "
              "Otherwise, open an issue on Discord."
      )
  );
}