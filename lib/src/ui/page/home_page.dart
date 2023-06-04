import 'package:bitsdojo_window/bitsdojo_window.dart' hide WindowBorder;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get/get_rx/src/rx_types/rx_types.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/ui/page/launcher_page.dart';
import 'package:reboot_launcher/src/ui/page/server_page.dart';
import 'package:reboot_launcher/src/ui/page/settings_page.dart';
import 'package:window_manager/window_manager.dart';

import '../controller/game_controller.dart';
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

class _HomePageState extends State<HomePage> with WindowListener, AutomaticKeepAliveClientMixin {
  static const double _kDefaultPadding = 12.0;
  static const int _kPagesLength = 5;
  
  final SettingsController _settingsController = Get.find<SettingsController>();
  final GlobalKey _searchKey = GlobalKey();
  final FocusNode _searchFocusNode = FocusNode();
  final TextEditingController _searchController = TextEditingController();
  final Rxn<List<NavigationPaneItem>> _searchItems = Rxn();
  final RxBool _focused = RxBool(true);
  final List<GlobalKey<NavigatorState>> _navigators = List.generate(_kPagesLength, (index) => GlobalKey());
  final List<RxInt> _navigationStatus = List.generate(_kPagesLength, (index) => RxInt(0));

  @override
  bool get wantKeepAlive => true;

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
  Widget build(BuildContext context) {
    super.build(context);
    return Stack(
      children: [
        LayoutBuilder(
            builder: (context, specs) => Obx(() => NavigationView(
                paneBodyBuilder: (pane, body) => Padding(
                    padding: const EdgeInsets.all(_kDefaultPadding),
                    child: body
                ),
                appBar: NavigationAppBar(
                    title: _draggableArea,
                    actions: WindowTitleBar(focused: _focused()),
                    automaticallyImplyLeading: false,
                    leading: _backButton
                ),
                pane: NavigationPane(
                  key: appKey,
                  selected: _selectedIndex,
                  onChanged: _onIndexChanged,
                  displayMode: specs.biggest.width <= 1536 ? PaneDisplayMode.compact : PaneDisplayMode.open,
                  items: _items,
                  footerItems: _footerItems,
                  autoSuggestBox: _autoSuggestBox,
                  autoSuggestBoxReplacement: const Icon(FluentIcons.search),
                ),
                onOpenSearch: () => _searchFocusNode.requestFocus(),
                transitionBuilder: (child, animation) => child
            ))
        ),
        if(isWin11)
          Obx(() => _focused.value ? const WindowBorder() : const SizedBox())
      ]
  );
  }

  Widget get _backButton => Obx(() {
    for(var entry in _navigationStatus){
      entry.value;
    }

    var onBack = _onBack();
    return PaneItem(
      enabled: onBack != null,
      icon: const Icon(FluentIcons.back, size: 14.0),
      body: const SizedBox.shrink(),
    ).build(
        context,
        false,
        onBack,
        displayMode: PaneDisplayMode.compact
    );
  });

  Function()? _onBack() {
    var navigator = _navigators[_settingsController.index.value].currentState;
    if(navigator == null || !navigator.mounted || !navigator.canPop()){
      return null;
    }

    var status = _navigationStatus[_settingsController.index.value];
    if(status.value <= 0){
      return null;
    }

    return () async {
      Navigator.pop(navigator.context);
      status.value -= 1;
    };
  }

  void _onIndexChanged(int index) {
    _navigationStatus[_settingsController.index()].value = 0;
    _settingsController.index.value = index;
  }

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
      return _settingsController.index();
    }

    if(_settingsController.index() >= _allItems.length){
      return null;
    }

    var indexOnScreen = searchItems.indexOf(_allItems[_settingsController.index()]);
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
        title: const Text("Tutorial"),
        icon: const Icon(FluentIcons.info),
        body: InfoPage(_navigators[0], _navigationStatus[0])
    ),
    PaneItem(
        title: const Text("Play"),
        icon: const Icon(FluentIcons.game),
        body: LauncherPage(_navigators[1], _navigationStatus[1])
    ),

    PaneItem(
        title: const Text("Host"),
        icon: const Icon(FluentIcons.server_processes),
        body: HostingPage(_navigators[2], _navigationStatus[2])
    ),

    PaneItem(
        title: const Text("Backend"),
        icon: const Icon(FluentIcons.user_window),
        body: ServerPage(_navigators[3], _navigationStatus[3])
    ),
  ];

  String get searchValue => _searchController.text;
}
