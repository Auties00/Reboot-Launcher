import 'package:bitsdojo_window/bitsdojo_window.dart' hide WindowBorder;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get/get_rx/src/rx_types/rx_types.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/ui/page/launcher_page.dart';
import 'package:reboot_launcher/src/ui/page/server_page.dart';
import 'package:reboot_launcher/src/ui/page/settings_page.dart';
import 'package:window_manager/window_manager.dart';

import '../controller/settings_controller.dart';
import '../widget/os/window_border.dart';
import '../widget/os/window_buttons.dart';
import 'hosting_page.dart';
import 'info_page.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WindowListener {
  static const double _defaultPadding = 12.0;

  final SettingsController _settingsController = Get.find<SettingsController>();

  final GlobalKey _searchKey = GlobalKey();
  final FocusNode _searchFocusNode = FocusNode();
  final TextEditingController _searchController = TextEditingController();
  final Rxn<List<NavigationPaneItem>> _searchItems = Rxn();
  final RxBool _focused = RxBool(true);
  final RxInt _index = RxInt(0);
  final RxBool _nestedNavigation = RxBool(false);
  final GlobalKey<NavigatorState> _settingsNavigatorKey = GlobalKey();

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
  Widget build(BuildContext context) => Obx(() => Stack(
      children: [
        NavigationView(
            paneBodyBuilder: (body) => Padding(
                padding: const EdgeInsets.all(_defaultPadding),
                child: body
            ),
            appBar: NavigationAppBar(
                title: _draggableArea,
                actions: WindowTitleBar(focused: _focused()),
                leading: _backButton
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

  Widget get _backButton => Obx(() {
    // ignore: unused_local_variable
    var ignored = _nestedNavigation.value;
    return PaneItem(
      icon: const Icon(FluentIcons.back, size: 14.0),
      body: const SizedBox.shrink(),
    ).build(
        context,
        false,
        _onBack(),
        displayMode: PaneDisplayMode.compact
    );
  });

  void Function()? _onBack() {
    var navigator = _settingsNavigatorKey.currentState;
    if(navigator == null || !navigator.mounted || !navigator.canPop()){
      return null;
    }

    return () async {
      Navigator.pop(navigator.context);
      _nestedNavigation.value = false;
    };
  }

  void _onIndexChanged(int index) => _index.value = index;

  TextBox get _autoSuggestBox => TextBox(
      key: _searchKey,
      controller: _searchController,
      placeholder: 'Search',
      focusNode: _searchFocusNode
  );

  GestureDetector get _draggableArea => GestureDetector(
      onDoubleTap: () => appWindow.maximizeOrRestore(),
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
        title: const Text("Play"),
        icon: const Icon(FluentIcons.game),
        body: const LauncherPage()
    ),

    PaneItem(
        title: const Text("Host"),
        icon: const Icon(FluentIcons.server_processes),
        body: const HostingPage()
    ),

    PaneItem(
        title: const Text("Backend"),
        icon: const Icon(FluentIcons.user_window),
        body: ServerPage()
    ),

    PaneItem(
        title: const Text("Tutorial"),
        icon: const Icon(FluentIcons.info),
        body: InfoPage(_settingsNavigatorKey, _nestedNavigation)
    ),
  ];

  String get searchValue => _searchController.text;
}
