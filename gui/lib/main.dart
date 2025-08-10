import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter_acrylic/flutter_acrylic.dart';
import 'package:flutter_localized_locales/flutter_localized_locales.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:local_notifier/local_notifier.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/dll_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/server_browser_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/message/error.dart';
import 'package:reboot_launcher/src/pager/pager.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/url_protocol.dart';
import 'package:system_theme/system_theme.dart';
import 'package:version/version.dart';
import 'package:window_manager/window_manager.dart';

import 'l10n/reboot_localizations.dart';

const double kDefaultWindowWidth = 1164;
const double kDefaultWindowHeight = 864;
const String kCustomUrlSchema = "Reboot";

Version? appVersion;
bool appWithNoStorage = false;

void main() {
  log("[APP] Called");
  runZonedGuarded(
          () => _startApp(),
          (error, stack) => onError(error, stack, false),
      zoneSpecification: ZoneSpecification(
          handleUncaughtError: (self, parent, zone, error, stacktrace) => onError(error, stacktrace, false)
      )
  );
}

// If anything fails here, the app won't start
// Be extremely careful
Future<void> _startApp() async {
  final errors = <String>[];
  Future<T?> runCatching<T>({
    required FutureOr<T> Function() callable,
    required String Function(Object) errorFormatter
  }) async {
    try {
      return callable();
    }catch(error) {
      errors.add(errorFormatter(error));
      return null;
    }
  }

  log("[APP] Starting application");
  try {
    WidgetsFlutterBinding.ensureInitialized();
    await runCatching(
        callable: () => installationDirectory.create(
            recursive: true
        ),
        errorFormatter: (error) => "Cannot create installation directory: $error"
    );
    await runCatching(
        callable: () => localNotifier.setup(
            appName: 'Reboot Launcher',
            shortcutPolicy: ShortcutPolicy.ignore
        ),
        errorFormatter: (error) => "Cannot create installation directory: $error"
    );
    await runCatching(
        callable: () async {
          final packageInfo = await PackageInfo.fromPlatform();
          appVersion = Version.parse(packageInfo.version);
        },
        errorFormatter: (error) => "Cannot parse version: $error"
    );
    await runCatching(
        callable: () async {
          await GetStorage(GameController.storageName, settingsDirectory.path).initStorage;
          await GetStorage(BackendController.storageName, settingsDirectory.path).initStorage;
          await GetStorage(SettingsController.storageName, settingsDirectory.path).initStorage;
          await GetStorage(HostingController.storageName, settingsDirectory.path).initStorage;
          await GetStorage(DllController.storageName, settingsDirectory.path).initStorage;
        },
        errorFormatter:   (error) {
          appWithNoStorage = true;
          return "Cannot access storage: $error";
        }
    );
    await runCatching(
        callable: () => Get.put(GameController(), permanent: true),
        errorFormatter: (error) => "Cannot create game controller: $error"
    );
    await runCatching(
        callable: () => Get.put(BackendController(), permanent: true),
        errorFormatter: (error) => "Cannot create backend controller: $error"
    );
    await runCatching(
        callable: () => Get.put(HostingController(), permanent: true),
        errorFormatter: (error) => "Cannot create backend controller: $error"
    );
    await runCatching(
        callable: () => Get.put(ServerBrowserController(), permanent: true),
        errorFormatter: (error) => "Cannot create browser controller: $error"
    );
    final settingsController = await runCatching(
        callable: () => Get.put(SettingsController(), permanent: true),
        errorFormatter: (error) => "Cannot create settings controller: $error"
    );
    await runCatching(
        callable: () => Get.put(DllController(), permanent: true),
        errorFormatter: (error) => "Cannot create dll controller: $error"
    );
    await runCatching(
        callable: () async {
          try {
            await SystemTheme.accentColor.load();
            await windowManager.ensureInitialized();
            await Window.initialize();
            if(settingsController != null) {
              final size = Size(settingsController.width, settingsController.height);
              await windowManager.setSize(size);
              final offsetX = settingsController.offsetX;
              final offsetY = settingsController.offsetY;
              if (offsetX != null && offsetY != null) {
                final position = Offset(
                    offsetX,
                    offsetY
                );
                await windowManager.setPosition(position);
              } else {
                await windowManager.setAlignment(Alignment.center);
              }
            }
            await windowManager.setPreventClose(true);
            await windowManager.setResizable(true);
            if(isWin11) {
              await Window.setEffect(
                  effect: WindowEffect.acrylic,
                  color: Colors.green,
                  dark: isDarkMode
              );
            }
          } finally {
            windowManager.show();
          }
        },
        errorFormatter: (error) => "Cannot configure window: $error"
    );
    runCatching(
        callable: () => registerUrlProtocol(kCustomUrlSchema, arguments: ['%s']),
        errorFormatter: (error) => "Cannot configure custom url scheme: $error"
    );
  }catch(error) {
    errors.add("Uncaught error: $error");
  }finally {
    log("[APP] Started applications with errors: $errors");
    runApp(RebootApplication(errors: errors));
  }
}

class RebootApplication extends StatefulWidget {
  final List<String> errors;
  const RebootApplication({Key? key, required this.errors}) : super(key: key);

  @override
  State<RebootApplication> createState() => _RebootApplicationState();
}

class _RebootApplicationState extends State<RebootApplication> {
  final SettingsController _settingsController = Get.find<SettingsController>();

  @override
  void initState() {
    super.initState();
    // Not pretty but make sure the errors are shown
    Future.delayed(const Duration(seconds: 5)).then((_) {
      for(final error in widget.errors) {
        onError(error, null, false);
      }
    });
  }

  @override
  Widget build(BuildContext context) => Obx(() => FluentApp(
      locale: Locale(_settingsController.language.value),
      localizationsDelegates: const [
        ...AppLocalizations.localizationsDelegates,
        LocaleNamesLocalizationsDelegate()
      ],
      supportedLocales: AppLocalizations.supportedLocales,
      themeMode: _settingsController.themeMode.value,
      debugShowCheckedModeBanner: false,
      color: SystemTheme.accentColor.accent.toAccentColor(),
      darkTheme: _createTheme(Brightness.dark),
      theme: _createTheme(Brightness.light),
      home: const RebootPager()
  ));

  FluentThemeData _createTheme(Brightness brightness) => FluentThemeData(
      brightness: brightness,
      accentColor: SystemTheme.accentColor.accent.toAccentColor(),
      visualDensity: VisualDensity.standard,
      scaffoldBackgroundColor: Colors.transparent
  );
}
