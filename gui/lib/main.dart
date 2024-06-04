import 'dart:async';
import 'dart:io';

import 'package:app_links/app_links.dart';
import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_acrylic/flutter_acrylic.dart';
import 'package:flutter_gen/gen_l10n/reboot_localizations.dart';
import 'package:flutter_localized_locales/flutter_localized_locales.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:local_notifier/local_notifier.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/controller/update_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/dialog/implementation/error.dart';
import 'package:reboot_launcher/src/dialog/implementation/server.dart';
import 'package:reboot_launcher/src/page/implementation/home_page.dart';
import 'package:reboot_launcher/src/page/implementation/info_page.dart';
import 'package:reboot_launcher/src/util/log.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:supabase_flutter/supabase_flutter.dart';
import 'package:system_theme/system_theme.dart';
import 'package:url_protocol/url_protocol.dart';
import 'package:version/version.dart';
import 'package:window_manager/window_manager.dart';
import 'package:win32/win32.dart';

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

Future<void> _startApp() async {
  final errors = <Object>[];
  try {
    log("[APP] Starting application");
    final pathError = await _initPath();
    if(pathError != null) {
      errors.add(pathError);
    }

    final databaseError = await _initDatabase();
    if(databaseError != null) {
      errors.add(databaseError);
    }

    final notificationsError = await _initNotifications();
    if(notificationsError != null) {
      errors.add(notificationsError);
    }

    final tilesError = InfoPage.initInfoTiles();
    if(tilesError != null) {
      errors.add(tilesError);
    }

    final versionError = await _initVersion();
    if(versionError != null) {
      errors.add(versionError);
    }

    final storageErrors = await _initStorage();
    errors.addAll(storageErrors);

    WidgetsFlutterBinding.ensureInitialized();

    _initWindow();

    final urlError = await _initUrlHandler();
    if(urlError != null) {
      errors.add(urlError);
    }
  }catch(uncaughtError) {
    errors.add(uncaughtError);
  } finally{
    log("[APP] Started applications with errors: $errors");
    runApp(RebootApplication(
      errors: errors,
    ));
  }
}

Future<Object?> _initNotifications() async {
  try {
    await localNotifier.setup(
        appName: 'Reboot Launcher',
        shortcutPolicy: ShortcutPolicy.ignore
    );
    return null;
  }catch(error) {
    return error;
  }
}

Future<Object?> _initDatabase() async {
  try {
    await Supabase.initialize(
        url: supabaseUrl,
        anonKey: supabaseAnonKey
    );
    return null;
  }catch(error) {
    return error;
  }
}

Future<Object?> _initPath() async {
  try {
    await installationDirectory.create(recursive: true);
    return null;
  }catch(error) {
    return error;
  }
}

Future<Object?> _initVersion() async {
  try {
    final packageInfo = await PackageInfo.fromPlatform();
    appVersion = Version.parse(packageInfo.version);
    return null;
  }catch(error) {
    return error;
  }
}

Future<Object?> _initUrlHandler() async {
  try {
    registerProtocolHandler(kCustomUrlSchema, arguments: ['%s']);
    return null;
  }catch(error) {
    return error;
  }
}

void _initWindow() => doWhenWindowReady(() async {
  try {
    await SystemTheme.accentColor.load();
    await windowManager.ensureInitialized();
    await Window.initialize();
    var settingsController = Get.find<SettingsController>();
    var size = Size(settingsController.width, settingsController.height);
    appWindow.size = size;
    var offsetX = settingsController.offsetX;
    var offsetY = settingsController.offsetY;
    if(offsetX != null && offsetY != null){
      appWindow.position = Offset(
          offsetX,
          offsetY
      );
    }else {
      appWindow.alignment = Alignment.center;
    }

    if(isWin11) {
      await Window.setEffect(
          effect: WindowEffect.acrylic,
          color: Colors.transparent,
          dark: SchedulerBinding.instance.platformDispatcher.platformBrightness.isDark
      );
    }
  }catch(error, stackTrace) {
    onError(error, stackTrace, false);
  }finally {
    appWindow.show();
  }
});

Future<List<Object>> _initStorage() async {
  final errors = <Object>[];
  try {
    await GetStorage("game", settingsDirectory.path).initStorage;
    await GetStorage("backend", settingsDirectory.path).initStorage;
    await GetStorage("update", settingsDirectory.path).initStorage;
    await GetStorage("settings", settingsDirectory.path).initStorage;
    await GetStorage("hosting", settingsDirectory.path).initStorage;
  }catch(error) {
    appWithNoStorage = true;
    errors.add("The Reboot Launcher configuration in ${settingsDirectory.path} cannot be accessed: running with in memory storage");
  }

  try {
    Get.put(GameController());
  }catch(error) {
    errors.add(error);
  }

  try {
    Get.put(BackendController());
  }catch(error) {
    errors.add(error);
  }

  try {
    Get.put(BuildController());
  }catch(error) {
    errors.add(error);
  }

  try {
    Get.put(HostingController());
  }catch(error) {
    errors.add(error);
  }

  try {
    Get.put(UpdateController());
  }catch(error) {
    errors.add(error);
  }

  try {
    Get.put(SettingsController());
  }catch(error) {
    errors.add(error);
  }


  return errors;
}

class RebootApplication extends StatefulWidget {
  final List<Object> errors;
  const RebootApplication({Key? key, required this.errors}) : super(key: key);

  @override
  State<RebootApplication> createState() => _RebootApplicationState();
}

class _RebootApplicationState extends State<RebootApplication> {
  final SettingsController _settingsController = Get.find<SettingsController>();

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((timeStamp) => _handleErrors(widget.errors));
  }

  void _handleErrors(List<Object?> errors) {
    errors.where((element) => element != null).forEach((element) => onError(element!, null, false));
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
      home: const HomePage()
  ));

  FluentThemeData _createTheme(Brightness brightness) => FluentThemeData(
      brightness: brightness,
      accentColor: SystemTheme.accentColor.accent.toAccentColor(),
      visualDensity: VisualDensity.standard,
      scaffoldBackgroundColor: Colors.transparent
  );
}
