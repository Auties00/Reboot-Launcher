import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:system_theme/system_theme.dart';
import 'package:reboot_launcher/src/page/home_page.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  SystemTheme.accentColor.load();
  doWhenWindowReady(() {
    const size = Size(600, 380);
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
