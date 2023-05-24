import 'dart:async';

import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:bitsdojo_window_windows/bitsdojo_window_windows.dart'
    show WinDesktopWindow;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/util/error.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/ui/controller/build_controller.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/ui/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/ui/controller/server_controller.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/page/home_page.dart';
import 'package:system_theme/system_theme.dart';

const double kDefaultWindowWidth = 885;
const double kDefaultWindowHeight = 885;
final GlobalKey appKey = GlobalKey();

void main() async {
  await installationDirectory.create(recursive: true);
  WidgetsFlutterBinding.ensureInitialized();
  await SystemTheme.accentColor.load();
  await GetStorage.init("reboot_game");
  await GetStorage.init("reboot_server");
  await GetStorage.init("reboot_update");
  await GetStorage.init("reboot_settings");
  await GetStorage.init("reboot_hosting");
  Get.put(GameController());
  Get.put(ServerController());
  Get.put(BuildController());
  Get.put(SettingsController());
  Get.put(HostingController());
  doWhenWindowReady(() {
    appWindow.minSize = const Size(kDefaultWindowWidth, kDefaultWindowHeight);
    var controller = Get.find<SettingsController>();
    var size = Size(controller.width, controller.height);
    var window = appWindow as WinDesktopWindow;
    window.setWindowCutOnMaximize(appBarSize * 2);
    appWindow.size = size;
    if(controller.offsetX != null && controller.offsetY != null){
      appWindow.position = Offset(controller.offsetX!, controller.offsetY!);
    }else {
      appWindow.alignment = Alignment.center;
    }

    appWindow.title = "Reboot Launcher";
    appWindow.show();
  });

  runZonedGuarded(
    () async => runApp(const RebootApplication()),
    (error, stack) => onError(error, stack, false),
    zoneSpecification: ZoneSpecification(
      handleUncaughtError: (self, parent, zone, error, stacktrace) => onError(error, stacktrace, false)
    )
  );
}

class RebootApplication extends StatefulWidget {
  const RebootApplication({Key? key}) : super(key: key);

  @override
  State<RebootApplication> createState() => _RebootApplicationState();
}

class _RebootApplicationState extends State<RebootApplication> {
  @override
  Widget build(BuildContext context) => FluentApp(
    title: "Reboot Launcher",
    themeMode: ThemeMode.system,
    debugShowCheckedModeBanner: false,
    color: SystemTheme.accentColor.accent.toAccentColor(),
    darkTheme: _createTheme(Brightness.dark),
    theme: _createTheme(Brightness.light),
    home: HomePage(key: appKey),
  );

  FluentThemeData _createTheme(Brightness brightness) => FluentThemeData(
    brightness: brightness,
    accentColor: SystemTheme.accentColor.accent.toAccentColor(),
    visualDensity: VisualDensity.standard,
    focusTheme: FocusThemeData(
      glowFactor: is10footScreen() ? 2.0 : 0.0,
    ),
  );
}
