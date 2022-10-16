
import 'package:bitsdojo_window/bitsdojo_window.dart' hide WindowBorder;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/settings_page.dart';
import 'package:reboot_launcher/src/page/launcher_page.dart';
import 'package:reboot_launcher/src/page/server_page.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/widget/window_border.dart';
import 'package:reboot_launcher/src/widget/window_buttons.dart';
import 'package:window_manager/window_manager.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WindowListener {
  static const double _headerSize = 48.0;
  static const double _sectionSize = 97.0;
  static const int _headerButtonCount = 3;
  static const int _sectionButtonCount = 3;

  bool _focused = true;
  bool _shouldMaximize = false;
  int _index = 0;

  @override
  void initState() {
    windowManager.addListener(this);
    super.initState();
  }

  @override
  void dispose() {
    windowManager.removeListener(this);
    super.dispose();
  }

  @override
  void onWindowFocus() {
    setState(() => _focused = true);
  }

  @override
  void onWindowBlur() {
    setState(() => _focused = !_focused);
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        NavigationView(
          pane: NavigationPane(
              size: const NavigationPaneSize(
                  topHeight: _headerSize
              ),
              selected: _index,
              onChanged: (index) => setState(() => _index = index),
              displayMode: PaneDisplayMode.top,
              indicator: const EndNavigationIndicator(),
              items: [
                PaneItem(
                    title: const Text("Home"),
                    icon: const Icon(FluentIcons.game),
                    body: const LauncherPage()
                ),

                PaneItem(
                    title: const Text("Lawin"),
                    icon: const Icon(FluentIcons.server_enviroment),
                    body: ServerPage()
                ),

                PaneItem(
                    title: const Text("Settings"),
                    icon: const Icon(FluentIcons.settings),
                    body: SettingsPage()
                )
              ]
          ),
        ),

        _createTitleBar(),

        _createGestureHandler(),

        if(_focused && isWin11)
          const WindowBorder()
      ],
    );
  }

  Align _createTitleBar() {
    return Align(
        alignment: Alignment.topRight,
        child: WindowTitleBar(focused: _focused),
      );
  }

  // Hacky way to get it to work while having maximum performance and no modifications to external libs
  Padding _createGestureHandler() {
    return Padding(
        padding: const EdgeInsets.only(
            left: _sectionSize * _headerButtonCount,
            right: _headerSize * _sectionButtonCount,
        ),
        child: SizedBox(
          height: _headerSize,
          child: GestureDetector(
              onDoubleTap: () {
                if(!_shouldMaximize){
                  return;
                }

                appWindow.maximizeOrRestore();
                _shouldMaximize = false;
              },
              onDoubleTapDown: (details) => _shouldMaximize = true,
              onHorizontalDragStart: (event) => appWindow.startDragging(),
              onVerticalDragStart: (event) => appWindow.startDragging()
          ),
        ),
      );
  }
}
