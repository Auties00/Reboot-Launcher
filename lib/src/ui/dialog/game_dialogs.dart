import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';

import '../../../main.dart';
import 'dialog.dart';

const String _unsupportedServerError = "The build you are currently using is not supported by Reboot. "
    "This means that you cannot currently host this version of the game. "
    "For a list of supported versions, check #info in the Discord server. "
    "If you are unsure which version works best, use build 7.40. "
    "If you are a passionate programmer you can add support by opening a PR on Github. ";

const String _corruptedBuildError = "The build you are currently using is corrupted. "
    "This means that some critical files are missing for the game to launch. "
    "Download the build again from the launcher or, if it's not available there, from another source. "
    "Occasionally some files might get corrupted if there isn't enough space on your drive.";

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
              "The game has been restarted automatically. "
      )
  );
}

Future<void> showTokenErrorCouldNotFix() async {
  showDialog(
      context: appKey.currentContext!,
      builder: (context) => const InfoDialog(
          text: "A token error occurred. "
              "The game couldn't be recovered, open an issue on Discord."
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

Future<void> showCorruptedBuildError(bool server, [Object? error, StackTrace? stackTrace]) async {
  if(error == null) {
    showDialog(
        context: appKey.currentContext!,
        builder: (context) => InfoDialog(
            text: server ? _unsupportedServerError : _corruptedBuildError
        )
    );
    return;
  }

  showDialog(
      context: appKey.currentContext!,
      builder: (context) => ErrorDialog(
          exception: error,
          stackTrace: stackTrace,
          errorMessageBuilder: (exception) => _corruptedBuildError
      )
  );
}

Future<void> showMissingBuildError(FortniteVersion version) async {
  showDialog(
      context: appKey.currentContext!,
      builder: (context) => InfoDialog(
          text: "${version.location.path} no longer contains a Fortnite executable. "
              "This probably means that you deleted it or move it somewhere else."
      )
  );
}