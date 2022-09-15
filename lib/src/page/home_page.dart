import 'package:bitsdojo_window/bitsdojo_window.dart' hide WindowBorder;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/info_page.dart';
import 'package:reboot_launcher/src/page/launcher_page.dart';
import 'package:reboot_launcher/src/page/server_page.dart';
import 'package:reboot_launcher/src/widget/window_buttons.dart';
import 'package:reboot_launcher/src/widget/window_border.dart';
import 'package:window_manager/window_manager.dart';

import '../util/os.dart';
import '../util/reboot.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WindowListener {
  late final Future _future;
  bool _focused = true;
  int _index = 0;

  @override
  void initState() {
    windowManager.addListener(this);
    _future = downloadRebootDll();
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
    setState(() => _focused = false);
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
                  _createPane("Launcher", FluentIcons.game),
                  _createPane("Server", FluentIcons.server_enviroment),
                  _createPane("Info", FluentIcons.info),
                ],
                trailing: WindowTitleBar(focused: _focused)),
            content: FutureBuilder(
                future: _future,
                builder: (context, snapshot) {
                  if (snapshot.hasError) {
                    return Center(
                        child: Text(
                            "An error occurred while loading the launcher: ${snapshot.error}",
                            textAlign: TextAlign.center));
                  }
                  return NavigationBody(
                      index: _index,
                      children: _createPages(snapshot.hasData));
                })
        ),

        if(_focused && isWin11)
          const WindowBorder()
      ],
    );
  }

  List<Widget> _createPages(bool data) {
    return [
      data ? const LauncherPage() : _createDownloadWarning(),
      const ServerPage(),
      const InfoPage()
    ];
  }

  Widget _createDownloadWarning() {
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: const [
            ProgressRing(),
            SizedBox(height: 16.0),
            Text("Updating Reboot DLL...")
          ],
        ),
      ],
    );
  }

  PaneItem _createPane(String label, IconData icon) {
    return PaneItem(icon: Icon(icon), title: Text(label));
  }
}
