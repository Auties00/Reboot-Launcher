import 'package:reboot_common/common.dart';

import '../abstract/dialog.dart';

const String _unsupportedServerError = "The build you are currently using is not supported by Reboot. "
    "If you are unsure which version works best, use build 7.40. "
    "If you are a passionate programmer you can add support by opening a PR on Github. ";

const String _corruptedBuildError = "An unknown occurred while launching Fortnite. "
    "Some critical files could be missing in your installation. "
    "Download the build again from the launcher, not locally, or from a different source. "
    "Alternatively, something could have gone wrong in the launcher. ";

Future<void> showMissingDllError(String name) async {
  showAppDialog(
      builder: (context) => InfoDialog(
          text: "$name dll is not a valid dll, fix it in the settings tab"
      )
  );
}

Future<void> showTokenErrorFixable() async {
  showAppDialog(
      builder: (context) => const InfoDialog(
          text: "A token error occurred. "
              "The backend server has been automatically restarted to fix the issue. "
              "The game has been restarted automatically. "
      )
  );
}

Future<void> showTokenErrorUnfixable() async {
  showAppDialog(
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
    showAppDialog(
        builder: (context) => InfoDialog(
            text: server ? _unsupportedServerError : _corruptedBuildError
        )
    );
    return;
  }

  showAppDialog(
      builder: (context) => ErrorDialog(
          exception: error,
          stackTrace: stackTrace,
          errorMessageBuilder: (exception) => "${_corruptedBuildError}Error message: $exception"
      )
  );
}

Future<void> showMissingBuildError(FortniteVersion version) async {
  showAppDialog(
      builder: (context) => InfoDialog(
          text: "${version.location.path} no longer contains a Fortnite executable. "
              "This probably means that you deleted it or move it somewhere else."
      )
  );
}