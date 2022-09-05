import 'dart:convert';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/util/game_process_controller.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:reboot_launcher/src/page/info_page.dart';
import 'package:reboot_launcher/src/page/launcher_page.dart';
import 'package:reboot_launcher/src/page/server_page.dart';
import 'package:reboot_launcher/src/widget/window_buttons.dart';

import '../model/fortnite_version.dart';
import '../util/generic_controller.dart';
import '../util/reboot.dart';
import '../util/version_controller.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  late final TextEditingController _usernameController;
  late final VersionController _versionController;
  late final GenericController<bool> _rebootController;
  late final GenericController<bool> _localController;
  late final TextEditingController _hostController;
  late final TextEditingController _portController;
  late final GameProcessController _gameProcessController;
  late final GenericController<Process?> _serverController;
  late final GenericController<bool> _startedServerController;
  late final GenericController<bool> _startedGameController;
  late Future _future;
  bool _loaded = false;
  int _index = 0;

  @override
  void initState(){
    _future = _load();
    super.initState();
  }

  Future<bool> _load() async {
    if (_loaded) {
      return false;
    }

    var preferences = await SharedPreferences.getInstance();
    await downloadRebootDll(preferences);

    Iterable json = jsonDecode(preferences.getString("versions") ?? "[]");
    var versions =
        json.map((entry) => FortniteVersion.fromJson(entry)).toList();
    var selectedVersion = preferences.getString("version");
    _versionController = VersionController(
        versions: versions,
        serializer: _saveVersions,
        selectedVersion: selectedVersion != null
            ? versions.firstWhere((element) => element.name == selectedVersion)
            : null);

    _rebootController =
        GenericController(initialValue: preferences.getBool("reboot") ?? false);

    _usernameController =
        TextEditingController(text: preferences.getString("${_rebootController.value ? "host" : "game"}_username"));

    _localController =
        GenericController(initialValue: preferences.getBool("local") ?? true);

    _hostController =
        TextEditingController(text: preferences.getString("host"));

    _portController =
        TextEditingController(text: preferences.getString("port"));

    _gameProcessController = GameProcessController();

    _serverController = GenericController(initialValue: null);

    _startedServerController = GenericController(initialValue: false);

    _startedGameController = GenericController(initialValue: false);

    _loaded = true;

    return true;
  }

  Future<void> _saveVersions() async {
    var preferences = await SharedPreferences.getInstance();
    var versions =
        _versionController.versions.map((entry) => entry.toJson()).toList();
    preferences.setString("versions", jsonEncode(versions));
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

            return NavigationBody(index: _index, children: [
              LauncherPage(
                  usernameController: _usernameController,
                  versionController: _versionController,
                  rebootController: _rebootController,
                  serverController: _serverController,
                  localController: _localController,
                  gameProcessController: _gameProcessController,
                  startedGameController: _startedGameController,
                  startedServerController: _startedServerController
              ),
              ServerPage(
                  localController: _localController,
                  hostController: _hostController,
                  portController: _portController,
                  serverController: _serverController,
                  startedServerController: _startedServerController
              ),
              const InfoPage()
            ]);
          }),
    );
  }

  PaneItem _createPane(String label, IconData icon) {
    return PaneItem(icon: Icon(icon), title: Text(label));
  }
}
