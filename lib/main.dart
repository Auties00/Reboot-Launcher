import 'dart:io';

import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:bitsdojo_window_windows/bitsdojo_window_windows.dart'
    show WinDesktopWindow;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/page/home_page.dart';
import 'package:reboot_launcher/src/util/binary.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:system_theme/system_theme.dart';

void main() async {
  await Directory(safeBinariesDirectory)
      .create(recursive: true);
  WidgetsFlutterBinding.ensureInitialized();
  await SystemTheme.accentColor.load();
  await GetStorage.init("game");
  await GetStorage.init("server");
  await GetStorage.init("update");
  Get.put(GameController());
  Get.put(ServerController());
  Get.put(BuildController());
  doWhenWindowReady(() {
    const size = Size(600, 365);
    var window = appWindow as WinDesktopWindow;
    window.setWindowCutOnMaximize(appBarSize * 2);
    appWindow.size = size;
    appWindow.alignment = Alignment.center;
    appWindow.title = "Reboot Launcher";
    appWindow.show();
  });
  runApp(const RebootApplication());
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
      darkTheme: ThemeData(
        brightness: Brightness.dark,
        accentColor: color,
        visualDensity: VisualDensity.standard,
        focusTheme: FocusThemeData(
          glowFactor: is10footScreen() ? 2.0 : 0.0,
        ),
      ),
      theme: ThemeData(
        brightness: Brightness.light,
        accentColor: color,
        visualDensity: VisualDensity.standard,
        focusTheme: FocusThemeData(
          glowFactor: is10footScreen() ? 2.0 : 0.0,
        ),
      ),
      home: const HomePage(),
    );
  }
}
