import 'dart:ui';

import 'package:bitsdojo_window/bitsdojo_window.dart' hide WindowBorder;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get/get_rx/src/rx_types/rx_types.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/page/settings_page.dart';
import 'package:reboot_launcher/src/page/launcher_page.dart';
import 'package:reboot_launcher/src/page/server_page.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/widget/os/window_border.dart';
import 'package:reboot_launcher/src/widget/os/window_buttons.dart';
import 'package:window_manager/window_manager.dart';

import '../controller/settings_controller.dart';
import '../model/tutorial_page.dart';
import 'info_page.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WindowListener {
  static const double _headerSize = 48.0;
  static const double _sectionSize = 100.0;
  static const double _defaultPadding = 12.0;
  static const int _headerButtonCount = 3;
  static const int _sectionButtonCount = 4;

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
    _searchController.addListener(() {
      if (searchValue.isEmpty) {
        _searchItems.value = null;
        return;
      }

      _searchItems.value = _allItems.whereType<PaneItem>()
          .where((item) => (item.title as Text).data!.toLowerCase().contains(searchValue.toLowerCase()))
          .toList()
          .cast<NavigationPaneItem>();
    });
    super.initState();
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
      },
    );
  }

  @override
  Widget build(BuildContext context) => NotificationListener<SizeChangedLayoutNotification>(
      onNotification: (notification) => _calculateSize(),
      child: SizeChangedLayoutNotifier(
          child: Obx(() => Stack(
              children: [
                _createNavigationView(),
                if(_settingsController.displayType() == PaneDisplayMode.top)
                  Align(
                      alignment: Alignment.topRight,
                      child: WindowTitleBar(focused: _focused())
                  ),
                if(_settingsController.displayType() == PaneDisplayMode.top)
                  _createTopDisplayGestures(),
                if(_focused() && isWin11)
                  const WindowBorder()
              ])
          )
      )
  );

  Padding _createTopDisplayGestures() => Padding(
      padding: const EdgeInsets.only(
        left: _sectionSize * _sectionButtonCount,
        right: _headerSize * _headerButtonCount,
      ),
      child: SizedBox(
        height: _headerSize,
        child: _createWindowGestures()
      )
  );

  GestureDetector _createWindowGestures({Widget? child}) => GestureDetector(
      onDoubleTap: () {
        if(!_shouldMaximize){
          return;
        }

        appWindow.maximizeOrRestore();
        _shouldMaximize = false;
      },
      onDoubleTapDown: (details) => _shouldMaximize = true,
      onHorizontalDragStart: (event) => appWindow.startDragging(),
      onVerticalDragStart: (event) => appWindow.startDragging(),
      child: child
  );

  NavigationView _createNavigationView() {
    return NavigationView(
        paneBodyBuilder: (body) => _createPage(body),
        pane: NavigationPane(
          size: const NavigationPaneSize(
              topHeight: _headerSize
          ),
          selected: _selectedIndex,
          onChanged: _onIndexChanged,
          displayMode: _settingsController.displayType(),
          items: _createItems(),
          indicator: const EndNavigationIndicator(),
          footerItems: _createFooterItems(),
          header: _settingsController.displayType() != PaneDisplayMode.open ? null : const SizedBox(height: _defaultPadding),
          autoSuggestBox: _createAutoSuggestBox(),
          autoSuggestBoxReplacement:  _settingsController.displayType() == PaneDisplayMode.top ? null : const Icon(FluentIcons.search),
        ),
        onOpenSearch: () => _searchFocusNode.requestFocus(),
        transitionBuilder: _settingsController.displayType() == PaneDisplayMode.top ? null : (child, animation) => child
    );
  }

  void _onIndexChanged(int index) {
    _index.value = index;
    _navigated = true;
  }

  TextBox? _createAutoSuggestBox() {
    if (_settingsController.displayType() == PaneDisplayMode.top) {
      return null;
    }

    return TextBox(
        key: _searchKey,
        controller: _searchController,
        placeholder: 'Search',
        focusNode: _searchFocusNode
    );
  }

  RenderObjectWidget _createPage(Widget? body) {
    if(_settingsController.displayType() == PaneDisplayMode.top){
      return Padding(
        padding: const EdgeInsets.all(_defaultPadding),
        child: body
      );
    }

    return Column(
      children: [
        Row(
          children: [
            Expanded(
                child: _createWindowGestures(
                    child: Container(
                        height: _headerSize,
                        color: Colors.transparent
                    )
                )
            ),

            WindowTitleBar(focused: _focused())
          ],
        ),

        Expanded(
            child: Padding(
                padding: const EdgeInsets.only(
                    left: _defaultPadding,
                    right: _defaultPadding,
                    bottom: _defaultPadding
                ),
                child: body
            )
        )
      ],
    );
  }

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

  List<NavigationPaneItem> get _allItems => [..._createItems(), ..._createFooterItems()];

  List<NavigationPaneItem> _createFooterItems() => searchValue.isNotEmpty ? [] : [
    if(_settingsController.displayType() != PaneDisplayMode.top)
      PaneItem(
          title: const Text("Tutorial"),
          icon: const Icon(FluentIcons.info),
          body: const InfoPage(),
          onTap: _onTutorial
      )
  ];

  List<NavigationPaneItem> _createItems() => _searchItems() ?? [
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
        title: const Text("Settings"),
        icon: const Icon(FluentIcons.settings),
        body: SettingsPage()
    ),

    if(_settingsController.displayType() == PaneDisplayMode.top)
      PaneItem(
          title: const Text("Tutorial"),
          icon: const Icon(FluentIcons.info),
          body: const InfoPage(),
          onTap: _onTutorial
      )
  ];

  void _onTutorial() {
    if(!_navigated){
      setState(() {
        _settingsController.tutorialPage.value = TutorialPage.start;
        _settingsController.scrollingDistance = 0;
      });
    }

    _navigated = false;
  }

  bool _calculateSize() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _settingsController.saveWindowSize();
      var width = window.physicalSize.width;
      PaneDisplayMode? newType;
      if (width <= 1000) {
        newType = PaneDisplayMode.top;
      } else if (width >= 1500) {
        newType = PaneDisplayMode.open;
      } else if (width > 1000) {
        newType = PaneDisplayMode.compact;
      }

      if(newType == null || newType == _settingsController.displayType()){
        return;
      }

      _settingsController.displayType.value = newType;
      _searchItems.value = null;
      _searchController.text = "";
    });

    return true;
  }

  String get searchValue => _searchController.text;
}
