import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/page/info_page.dart';
import 'package:reboot_launcher/src/page/launcher_page.dart';
import 'package:reboot_launcher/src/page/server_page.dart';
import 'package:reboot_launcher/src/widget/window_buttons.dart';

import '../util/reboot.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final List<Widget> _children = [LauncherPage(), ServerPage(), const InfoPage()];
  late final Future _future;
  int _index = 0;

  @override
  void initState() {
    _future = downloadRebootDll();
    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    return NavigationView(
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
            trailing: const WindowTitleBar()),
        content: FutureBuilder(
          future: _future,
          builder: (context, snapshot) {
            if (snapshot.hasError) {
              return Center(
                  child: Text(
                      "An error occurred while loading the launcher: ${snapshot.error}",
                      textAlign: TextAlign.center));
            }

            if (!snapshot.hasData) {
              return const Center(child: ProgressRing());
            }

            return NavigationBody(
                index: _index,
                children: _children
            );
          }
        )
    );
  }

  PaneItem _createPane(String label, IconData icon) {
    return PaneItem(icon: Icon(icon), title: Text(label));
  }
}
