import 'package:bitsdojo_window/bitsdojo_window.dart' hide WindowBorder;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get/get_rx/src/rx_types/rx_types.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/page/settings_page.dart';
import 'package:reboot_launcher/src/page/launcher_page.dart';
import 'package:reboot_launcher/src/page/server_page.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/widget/os/window_border.dart';
import 'package:reboot_launcher/src/widget/os/window_buttons.dart';
import 'package:window_manager/window_manager.dart';

import '../controller/settings_controller.dart';
import '../model/server_type.dart';
import 'info_page.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WindowListener {
  static const double _defaultPadding = 12.0;

  final GameController _gameController = Get.find<GameController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final ServerController _serverController = Get.find<ServerController>();

  final GlobalKey _searchKey = GlobalKey();
  final FocusNode _searchFocusNode = FocusNode();
  final TextEditingController _searchController = TextEditingController();
  final Rxn<List<NavigationPaneItem>> _searchItems = Rxn();
  final RxBool _focused = RxBool(true);
  final RxInt _index = RxInt(0);
  bool _navigated = false;
  bool _shouldMaximize = false;

  @override
  void initState() {
    windowManager.addListener(this);
    _searchController.addListener(_onSearch);
    super.initState();
  }

  void _onSearch() {
    if (searchValue.isEmpty) {
      _searchItems.value = null;
      return;
    }

    _searchItems.value = _allItems.whereType<PaneItem>()
        .where((item) => (item.title as Text).data!.toLowerCase().contains(searchValue.toLowerCase()))
        .toList()
        .cast<NavigationPaneItem>();
  }

  @override
  void dispose() {
    windowManager.removeListener(this);
    _searchFocusNode.dispose();
    _searchController.dispose();
    super.dispose();
  }

  @override
  void onWindowFocus() {
    _focused.value = true;
  }

  @override
  void onWindowBlur() {
    _focused.value = false;
  }

  @override
  void onWindowResized() {
    _settingsController.saveWindowSize();
    super.onWindowResized();
  }

  @override
  void onWindowMoved() {
    _settingsController.saveWindowOffset(appWindow.position);
    super.onWindowMoved();
  }

  @override
  void onWindowClose() async {
    if(!_gameController.started() || !_serverController.started()) {
      windowManager.destroy();
      return;
    }

    showDialog(
      context: context,
      builder: (_) {
        return InfoDialog(
          text: "Closing the launcher while a backend is running may make the game not work correctly. Are you sure you want to proceed?",
          buttons: [
            DialogButton(
              type: ButtonType.secondary,
              text: "Don't close",
            ),

            DialogButton(
              type: ButtonType.primary,
              onTap: () => windowManager.destroy(),
              text: "Close",
            ),
          ],
        );
      }
    );
  }

  @override
  Widget build(BuildContext context) => Obx(() => Stack(
      children: [
        NavigationView(
            paneBodyBuilder: (body) => Padding(
                padding: const EdgeInsets.all(_defaultPadding),
                child: body
            ),
            appBar: NavigationAppBar(
                title: _draggableArea,
                actions: WindowTitleBar(focused: _focused())
            ),
            pane: NavigationPane(
              selected: _selectedIndex,
              onChanged: _onIndexChanged,
              displayMode: PaneDisplayMode.auto,
              items: _items,
              footerItems: _footerItems,
              autoSuggestBox: _autoSuggestBox,
              autoSuggestBoxReplacement: const Icon(FluentIcons.search),
            ),
            onOpenSearch: () => _searchFocusNode.requestFocus(),
            transitionBuilder: (child, animation) => child
        ),
        if(_focused() && isWin11)
          const WindowBorder()
      ]
  ));

  void _onIndexChanged(int index) {
    _index.value = index;
    _navigated = true;
  }

  TextBox get _autoSuggestBox => TextBox(
      key: _searchKey,
      controller: _searchController,
      placeholder: 'Search',
      focusNode: _searchFocusNode
  );

  GestureDetector get _draggableArea => GestureDetector(
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
  );

  int? get _selectedIndex {
    var searchItems = _searchItems();
    if (searchItems == null) {
      return _index();
    }

    if(_index() >= _allItems.length){
      return null;
    }

    var indexOnScreen = searchItems.indexOf(_allItems[_index()]);
    if (indexOnScreen.isNegative) {
      return null;
    }

    return indexOnScreen;
  }

  List<NavigationPaneItem> get _allItems => [..._items, ..._footerItems];

  List<NavigationPaneItem> get _footerItems => searchValue.isNotEmpty ? [] : [
      PaneItem(
          title: const Text("Settings"),
          icon: const Icon(FluentIcons.settings),
          body: SettingsPage()
      )
  ];

  List<NavigationPaneItem> get _items => _searchItems() ?? [
    PaneItem(
        title: const Text("Home"),
        icon: const Icon(FluentIcons.game),
        body: const LauncherPage()
    ),

    PaneItem(
        title: const Text("Backend"),
        icon: const Icon(FluentIcons.server_enviroment),
        body: ServerPage()
    ),

    PaneItem(
        title: const Text("Tutorial"),
        icon: const Icon(FluentIcons.info),
        body: const InfoPage(),
        onTap: _onTutorial
    ),
  ];

  void _onTutorial() {
    if(!_navigated){
      setState(() => _settingsController.scrollingDistance = 0);
    }

    _navigated = false;
  }

  String get searchValue => _searchController.text;
}
