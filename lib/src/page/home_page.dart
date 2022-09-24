import 'dart:ui';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/page/info_page.dart';
import 'package:reboot_launcher/src/page/launcher_page.dart';
import 'package:reboot_launcher/src/page/server_page.dart';
import 'package:reboot_launcher/src/widget/window_buttons.dart';
import 'package:reboot_launcher/src/widget/window_border.dart';
import 'package:window_manager/window_manager.dart';

import 'package:reboot_launcher/src/util/os.dart';
import 'package:get/get.dart';

import '../controller/build_controller.dart';
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
    var storage = GetStorage("update");
    int? lastUpdateMs = storage.read("last_update");
    _future = compute(downloadRebootDll, lastUpdateMs);
    _future.then((value) => storage.write("last_update", value));
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
                  _createPane("Home", FluentIcons.game),
                  _createPane("Lawin", FluentIcons.server_enviroment),
                  _createPane("Info", FluentIcons.info),
                ],
                trailing: WindowTitleBar(focused: _focused)),
            content: FutureBuilder(
                future: _future,
                builder: (context, snapshot) => NavigationBody(
                      index: _index,
                      children: _createPages(snapshot)
                )
            )
        ),

        if(_focused && isWin11)
          const WindowBorder()
      ],
    );
  }

  List<Widget> _createPages(AsyncSnapshot snapshot) {

    return [
      LauncherPage(
          ready: snapshot.hasData,
          error: snapshot.error,
          stackTrace: snapshot.stackTrace
      ),
      ServerPage(),
      const InfoPage()
    ];
  }

  PaneItem _createPane(String label, IconData icon) {
    return PaneItem(icon: Icon(icon), title: Text(label));
  }
}
