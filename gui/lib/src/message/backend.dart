import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/messenger/info_bar.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:url_launcher/url_launcher.dart';

InfoBarEntry? onBackendResult(AuthBackendType type, AuthBackendResult event) {
  switch (event.type) {
    case AuthBackendResultType.starting:
      return showRebootInfoBar(
          translations.startingServer,
          severity: InfoBarSeverity.info,
          loading: true,
          duration: null
      );
    case AuthBackendResultType.startSuccess:
      return showRebootInfoBar(
          type == AuthBackendType.local
              ? translations.checkedServer
              : translations.startedServer,
          severity: InfoBarSeverity.success
      );
    case AuthBackendResultType.startError:
      return showRebootInfoBar(
          type == AuthBackendType.local
              ? translations.localServerError(event.error ?? translations.unknownError)
              : translations.startServerError(event.error ?? translations.unknownError),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration
      );
    case AuthBackendResultType.stopping:
      return showRebootInfoBar(
          translations.stoppingServer,
          severity: InfoBarSeverity.info,
          loading: true,
          duration: null
      );
    case AuthBackendResultType.stopSuccess:
      return showRebootInfoBar(
          translations.stoppedServer,
          severity: InfoBarSeverity.success
      );
    case AuthBackendResultType.stopError:
      return showRebootInfoBar(
          translations.stopServerError(event.error ?? translations.unknownError),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration
      );
    case AuthBackendResultType.startMissingHostError:
      return showRebootInfoBar(
          translations.missingHostNameError,
          severity: InfoBarSeverity.error
      );
    case AuthBackendResultType.startMissingPortError:
      return showRebootInfoBar(
          translations.missingPortError,
          severity: InfoBarSeverity.error
      );
    case AuthBackendResultType.startIllegalPortError:
      return showRebootInfoBar(
          translations.illegalPortError,
          severity: InfoBarSeverity.error
      );
    case AuthBackendResultType.startFreeingPort:
      return showRebootInfoBar(
          translations.freeingPort,
          loading: true,
          duration: null
      );
    case AuthBackendResultType.startFreePortSuccess:
      return showRebootInfoBar(
          translations.freedPort,
          severity: InfoBarSeverity.success,
          duration: infoBarShortDuration
      );
    case AuthBackendResultType.startFreePortError:
      return showRebootInfoBar(
          translations.freePortError(event.error ?? translations.unknownError),
          severity: InfoBarSeverity.error,
          duration: infoBarLongDuration
      );
    case AuthBackendResultType.startPingingRemote:
      return showRebootInfoBar(
          translations.pingingServer(AuthBackendType.remote.name),
          severity: InfoBarSeverity.info,
          loading: true,
          duration: null
      );
    case AuthBackendResultType.startPingingLocal:
      return showRebootInfoBar(
          translations.pingingServer(type.name),
          severity: InfoBarSeverity.info,
          loading: true,
          duration: null
      );
    case AuthBackendResultType.startPingError:
      return showRebootInfoBar(
          translations.pingError(type.name),
          severity: InfoBarSeverity.error
      );
    case AuthBackendResultType.startedImplementation:
      return null;
    }
}

void onBackendError(Object error) {
    showRebootInfoBar(
        translations.backendErrorMessage,
        severity: InfoBarSeverity.error,
        duration: infoBarLongDuration,
        action: Button(
          onPressed: () => launchUrl(launcherLogFile.uri),
          child: Text(translations.openLog),
        )
    );
}