
import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/info_page.dart';
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
  bool _focused = true;
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
                selected: _index,
                onChanged: (index) => setState(() => _index = index),
                displayMode: PaneDisplayMode.top,
                indicator: const EndNavigationIndicator(),
                items: [
                  _createPane("Home", FluentIcons.game),
                  _createPane("Lawin", FluentIcons.server_enviroment),
                  _createPane("Info", FluentIcons.info),
                ],
                trailing: WindowTitleBar(focused: _focused)),
            content: NavigationBody(
                index: _index,
                children: [
                  const LauncherPage(),
                  ServerPage(),
                  const InfoPage()
                ]
            )
        ),

        if(_focused && isWin11)
          const WindowBorder()
      ],
    );
  }

  PaneItem _createPane(String label, IconData icon) {
    return PaneItem(icon: Icon(icon), title: Text(label));
  }
}
