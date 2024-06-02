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
import 'package:reboot_launcher/src/controller/info_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/controller/update_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/dialog/implementation/error.dart';
import 'package:reboot_launcher/src/dialog/implementation/server.dart';
import 'package:reboot_launcher/src/page/implementation/home_page.dart';
import 'package:reboot_launcher/src/util/info.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:supabase_flutter/supabase_flutter.dart';
import 'package:system_theme/system_theme.dart';
import 'package:url_protocol/url_protocol.dart';
import 'package:version/version.dart';
import 'package:window_manager/window_manager.dart';

const double kDefaultWindowWidth = 1536;
const double kDefaultWindowHeight = 1024;
const String kCustomUrlSchema = "Reboot";

Version? appVersion;

class _MyHttpOverrides extends HttpOverrides {
  @override
  HttpClient createHttpClient(SecurityContext? context){
    return super.createHttpClient(context)
      ..badCertificateCallback = ((X509Certificate cert, String host, int port) => true);
  }
}

void main() => runZonedGuarded(
    () async {
      HttpOverrides.global = _MyHttpOverrides();
      final errors = <Object>[];
      try {
        await installationDirectory.create(recursive: true);
        await Supabase.initialize(
            url: supabaseUrl,
            anonKey: supabaseAnonKey
        );
        await localNotifier.setup(
          appName: 'Reboot Launcher',
          shortcutPolicy: ShortcutPolicy.ignore
        );
        WidgetsFlutterBinding.ensureInitialized();
        await SystemTheme.accentColor.load();
        _initWindow();
        initInfoTiles();
        final versionError = await _initVersion();
        if(versionError != null) {
          errors.add(versionError);
        }

        final storageError = await _initStorage();
        if(storageError != null) {
          errors.add(storageError);
        }

        final urlError = await _initUrlHandler();
        if(urlError != null) {
          errors.add(urlError);
        }

        _checkGameServer();
      }catch(uncaughtError) {
        errors.add(uncaughtError);
      } finally{
        runApp(const RebootApplication());
        WidgetsBinding.instance.addPostFrameCallback((timeStamp) => _handleErrors(errors));
      }
    },
    (error, stack) => onError(error, stack, false),
    zoneSpecification: ZoneSpecification(
       handleUncaughtError: (self, parent, zone, error, stacktrace) => onError(error, stacktrace, false)
    )
);

void _handleErrors(List<Object?> errors) {
  errors.where((element) => element != null).forEach((element) => onError(element!, null, false));
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

Future<void> _checkGameServer() async {
  try {
    var backendController = Get.find<BackendController>();
    var address = backendController.gameServerAddress.text;
    if(isLocalHost(address)) {
      return;
    }

    var result = await pingGameServer(address);
    if(result) {
      return;
    }

    var oldOwner = backendController.gameServerOwner.value;
    backendController.joinLocalHost();
    WidgetsBinding.instance.addPostFrameCallback((_) => showInfoBar(
        oldOwner == null ? translations.serverNoLongerAvailableUnnamed : translations.serverNoLongerAvailable(oldOwner),
        severity: InfoBarSeverity.warning,
        duration: infoBarLongDuration
    ));
  }catch(_) {
    // Intended behaviour
    // Just ignore the error
  }
}

Future<Object?> _initUrlHandler() async {
  try {
    registerProtocolHandler(kCustomUrlSchema, arguments: ['%s']);
    var appLinks = AppLinks();
    var initialUrl = await appLinks.getInitialLink();
    if(initialUrl != null) {
      _joinServer(initialUrl);
    }

    appLinks.uriLinkStream.listen(_joinServer);
    return null;
  }catch(error) {
    return error;
  }
}

void _joinServer(Uri uri) {
  var hostingController = Get.find<HostingController>();
  var backendController = Get.find<BackendController>();
  var uuid = _parseCustomUrl(uri);
  var server = hostingController.findServerById(uuid);
  if(server != null) {
    backendController.joinServer(hostingController.uuid, server);
  }else {
    showInfoBar(
        translations.noServerFound,
        duration: infoBarLongDuration,
        severity: InfoBarSeverity.error
    );
  }
}

String _parseCustomUrl(Uri uri) => uri.host;

void _initWindow() => doWhenWindowReady(() async {
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

  appWindow.show();
});

Future<Object?> _initStorage() async {
  try {
    await GetStorage("game", settingsDirectory.path).initStorage;
    await GetStorage("backend", settingsDirectory.path).initStorage;
    await GetStorage("update", settingsDirectory.path).initStorage;
    await GetStorage("settings", settingsDirectory.path).initStorage;
    await GetStorage("hosting", settingsDirectory.path).initStorage;
    Get.put(GameController());
    Get.put(BackendController());
    Get.put(BuildController());
    Get.put(SettingsController());
    Get.put(HostingController());
    Get.put(InfoController());
    Get.put(UpdateController());
    return null;
  }catch(error) {
    return error;
  }
}

class RebootApplication extends StatefulWidget {
  const RebootApplication({Key? key}) : super(key: key);

  @override
  State<RebootApplication> createState() => _RebootApplicationState();
}

class _RebootApplicationState extends State<RebootApplication> {
  final SettingsController _settingsController = Get.find<SettingsController>();

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
