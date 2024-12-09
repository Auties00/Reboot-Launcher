import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter_acrylic/flutter_acrylic.dart';
import 'package:flutter_gen/gen_l10n/reboot_localizations.dart';
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
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/messenger/implementation/error.dart';
import 'package:reboot_launcher/src/page/implementation/home_page.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/url_protocol.dart';
import 'package:supabase_flutter/supabase_flutter.dart';
import 'package:system_theme/system_theme.dart';
import 'package:version/version.dart';
import 'package:window_manager/window_manager.dart';

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
  _overrideHttpCertificate();
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

class _MyHttpOverrides extends HttpOverrides {
  @override
  HttpClient createHttpClient(SecurityContext? context){
    return super.createHttpClient(context)
      ..badCertificateCallback = ((X509Certificate cert, String host, int port) => true);
  }
}

void _overrideHttpCertificate() {
  HttpOverrides.global = _MyHttpOverrides(); // Not safe, but necessary
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
    registerUrlProtocol(kCustomUrlSchema, arguments: ['%s']);
    return null;
  }catch(error) {
    return error;
  }
}

Future<void> _initWindow() async {
  try {
    await SystemTheme.accentColor.load();
    await windowManager.ensureInitialized();
    await Window.initialize();
    var settingsController = Get.find<SettingsController>();
    var size = Size(settingsController.width, settingsController.height);
    await windowManager.setSize(size);
    var offsetX = settingsController.offsetX;
    var offsetY = settingsController.offsetY;
    if(offsetX != null && offsetY != null) {
      final position = Offset(
          offsetX,
          offsetY
      );
      await windowManager.setPosition(position);
    }else {
      await windowManager.setAlignment(Alignment.center);
    }
    await windowManager.setPreventClose(true);

    if(isWin11) {
      await Window.setEffect(
          effect: WindowEffect.acrylic,
          color: Colors.transparent,
          dark: isDarkMode
      );
    }
  }catch(error, stackTrace) {
    onError(error, stackTrace, false);
  }finally {
    windowManager.show();
  }
}

Future<List<Object>> _initStorage() async {
  final errors = <Object>[];
  try {
    await GetStorage(GameController.storageName, settingsDirectory.path).initStorage;
    await GetStorage(BackendController.storageName, settingsDirectory.path).initStorage;
    await GetStorage(SettingsController.storageName, settingsDirectory.path).initStorage;
    await GetStorage(HostingController.storageName, settingsDirectory.path).initStorage;
    await GetStorage(DllController.storageName, settingsDirectory.path).initStorage;
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
    final controller = HostingController();
    Get.put(controller);
    controller.discardServer();
  }catch(error) {
    errors.add(error);
  }

  try {
    Get.put(SettingsController());
  }catch(error) {
    errors.add(error);
  }

  try {
    Get.put(DllController());
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
