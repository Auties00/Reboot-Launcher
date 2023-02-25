import 'dart:async';
import 'dart:io';

import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:bitsdojo_window_windows/bitsdojo_window_windows.dart'
    show WinDesktopWindow;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/cli.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/page/home_page.dart';
import 'package:reboot_launcher/src/util/error.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:system_theme/system_theme.dart';
import 'package:window_manager/window_manager.dart';

final GlobalKey appKey = GlobalKey();

void main() async {
  await safeBinariesDirectory.create(recursive: true);
  WidgetsFlutterBinding.ensureInitialized();
  await SystemTheme.accentColor.load();
  await GetStorage.init("game");
  await GetStorage.init("server");
  await GetStorage.init("update");
  await GetStorage.init("settings");
  Get.put(GameController());
  Get.put(ServerController());
  Get.put(BuildController());
  Get.put(SettingsController());
  doWhenWindowReady(() {
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

    windowManager.setPreventClose(true);
    appWindow.title = "Reboot Launcher";
    appWindow.show();
  });

  runZonedGuarded(() =>
      runApp(const RebootApplication()),
      (error, stack) => onError(error, stack, false)
  );
}

class RebootApplication extends StatefulWidget {
  const RebootApplication({Key? key}) : super(key: key);

  @override
  State<RebootApplication> createState() => _RebootApplicationState();
}

class _RebootApplicationState extends State<RebootApplication> {
  @override
  Widget build(BuildContext context) {
    final color = SystemTheme.accentColor.accent.toAccentColor();
    return FluentApp(
      title: "Reboot Launcher",
      themeMode: ThemeMode.system,
      debugShowCheckedModeBanner: false,
      color: color,
      darkTheme: _createTheme(Brightness.dark),
      theme: _createTheme(Brightness.light),
      home: HomePage(key: appKey),
    );
  }

  FluentThemeData _createTheme(Brightness brightness) {
    return FluentThemeData(
      brightness: brightness,
      accentColor: SystemTheme.accentColor.accent.toAccentColor(),
      visualDensity: VisualDensity.standard,
      focusTheme: FocusThemeData(
        glowFactor: is10footScreen() ? 2.0 : 0.0,
      ),
    );
  }
}
