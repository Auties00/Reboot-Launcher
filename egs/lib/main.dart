import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter_acrylic/flutter_acrylic.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:system_theme/system_theme.dart';
import 'package:window_manager/window_manager.dart';

import 'src/controllers/server_controller.dart';
import 'src/pages/home_page.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Initialiser GetStorage
  await GetStorage.init('egs_storage');

  // Initialiser le thème système
  await SystemTheme.accentColor.load();

  // Configuration de la fenêtre
  await windowManager.ensureInitialized();

  const windowOptions = WindowOptions(
    size: Size(1200, 800),
    minimumSize: Size(800, 600),
    center: true,
    backgroundColor: Colors.transparent,
    skipTaskbar: false,
    title: 'EGS - Epic Game Server',
    titleBarStyle: TitleBarStyle.hidden,
  );

  windowManager.waitUntilReadyToShow(windowOptions, () async {
    await windowManager.show();
    await windowManager.focus();
  });

  // Configuration de l'effet acrylic (Windows 11)
  await Window.initialize();
  await Window.setEffect(
    effect: WindowEffect.acrylic,
    color: const Color(0xCC222222),
  );

  // Initialiser les contrôleurs
  Get.put(ServerController());

  runApp(const EgsApp());
}

class EgsApp extends StatelessWidget {
  const EgsApp({super.key});

  @override
  Widget build(BuildContext context) {
    return FluentApp(
      title: 'EGS - Epic Game Server',
      debugShowCheckedModeBanner: false,
      theme: FluentThemeData(
        brightness: Brightness.dark,
        accentColor: SystemTheme.accentColor.accent.toAccentColor(),
        visualDensity: VisualDensity.standard,
        focusTheme: FocusThemeData(
          glowFactor: is10footScreen(context) ? 2.0 : 0.0,
        ),
      ),
      home: const EgsHomePage(),
    );
  }
}

class EgsHomePage extends StatefulWidget {
  const EgsHomePage({super.key});

  @override
  State<EgsHomePage> createState() => _EgsHomePageState();
}

class _EgsHomePageState extends State<EgsHomePage> with WindowListener {
  @override
  void initState() {
    super.initState();
    windowManager.addListener(this);
  }

  @override
  void dispose() {
    windowManager.removeListener(this);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return NavigationView(
      appBar: NavigationAppBar(
        title: DragToMoveArea(
          child: Align(
            alignment: Alignment.centerLeft,
            child: Row(
              children: [
                Image.asset(
                  'assets/icons/icon.png',
                  width: 24,
                  height: 24,
                  errorBuilder: (context, error, stackTrace) {
                    return const Icon(FluentIcons.server, size: 24);
                  },
                ),
                const SizedBox(width: 12),
                const Text('EGS - Epic Game Server'),
              ],
            ),
          ),
        ),
        actions: Row(
          mainAxisAlignment: MainAxisAlignment.end,
          children: [
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 8.0),
              child: Row(
                children: const [
                  _WindowButton(
                    icon: FluentIcons.chrome_minimize,
                    onPressed: _minimizeWindow,
                  ),
                  _WindowButton(
                    icon: FluentIcons.checkbox_composite,
                    onPressed: _maximizeWindow,
                  ),
                  _WindowButton(
                    icon: FluentIcons.chrome_close,
                    onPressed: _closeWindow,
                    isClose: true,
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
      content: const HomePage(),
    );
  }

  static void _minimizeWindow() {
    windowManager.minimize();
  }

  static void _maximizeWindow() async {
    if (await windowManager.isMaximized()) {
      windowManager.unmaximize();
    } else {
      windowManager.maximize();
    }
  }

  static void _closeWindow() {
    windowManager.close();
  }
}

class _WindowButton extends StatelessWidget {
  final IconData icon;
  final VoidCallback onPressed;
  final bool isClose;

  const _WindowButton({
    required this.icon,
    required this.onPressed,
    this.isClose = false,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 46,
      height: 32,
      child: Button(
        onPressed: onPressed,
        style: ButtonStyle(
          backgroundColor: WidgetStateProperty.resolveWith((states) {
            if (states.isHovering) {
              return isClose ? Colors.red : Colors.grey[60];
            }
            return Colors.transparent;
          }),
          padding: WidgetStateProperty.all(EdgeInsets.zero),
        ),
        child: Icon(icon, size: 12),
      ),
    );
  }
}
