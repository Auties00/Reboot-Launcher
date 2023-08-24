import 'dart:async';

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
import 'package:reboot_launcher/supabase.dart';
import 'package:supabase_flutter/supabase_flutter.dart';
import 'package:system_theme/system_theme.dart';
import 'package:flutter_acrylic/flutter_acrylic.dart';
import 'package:window_manager/window_manager.dart';

const double kDefaultWindowWidth = 1024;
const double kDefaultWindowHeight = 1024;
final GlobalKey appKey = GlobalKey();

void main() async {
  runZonedGuarded(() async {
    await installationDirectory.create(recursive: true);
    await Supabase.initialize(
        url: supabaseUrl,
        anonKey: supabaseAnonKey
    );
    WidgetsFlutterBinding.ensureInitialized();
    await SystemTheme.accentColor.load();
    await GetStorage.init("reboot_game");
    await GetStorage.init("reboot_server");
    await GetStorage.init("reboot_update");
    await GetStorage.init("reboot_settings");
    await GetStorage.init("reboot_hosting");
    var gameController = GameController();
    Get.put(gameController);
    Get.put(ServerController());
    Get.put(BuildController());
    Get.put(SettingsController());
    Get.put(HostingController());
    await windowManager.ensureInitialized();
    var controller = Get.find<SettingsController>();
    var size = Size(controller.width, controller.height);
    await windowManager.setSize(size);
    if(controller.offsetX != null && controller.offsetY != null){
      await windowManager.setPosition(Offset(controller.offsetX!, controller.offsetY!));
    }else {
      await windowManager.setAlignment(Alignment.center);
    };
    await Window.initialize();
    await Window.setEffect(
        effect: WindowEffect.acrylic,
        color: Colors.transparent,
        dark: SystemTheme.isDarkMode
    );
    var supabase = Supabase.instance.client;
    await supabase.from('hosts')
        .delete()
        .match({'id': gameController.uuid});
    runApp(const RebootApplication());
  },
  (error, stack) => onError(error, stack, false),
  zoneSpecification: ZoneSpecification(
     handleUncaughtError: (self, parent, zone, error, stacktrace) => onError(error, stacktrace, false)
  ));
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
      home: const HomePage()
  );

  FluentThemeData _createTheme(Brightness brightness) => FluentThemeData(
      brightness: brightness,
      accentColor: SystemTheme.accentColor.accent.toAccentColor(),
      visualDensity: VisualDensity.standard,
      scaffoldBackgroundColor: Colors.transparent
  );
}
