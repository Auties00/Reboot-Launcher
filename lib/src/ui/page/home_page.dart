import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/ui/page/launcher_page.dart';
import 'package:reboot_launcher/src/ui/page/server_page.dart';
import 'package:reboot_launcher/src/ui/page/settings_page.dart';
import 'package:reboot_launcher/src/ui/widget/shared/profile_widget.dart';

import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/widget/os/window_border.dart';
import 'package:reboot_launcher/src/ui/widget/os/window_title_bar.dart';
import 'package:window_manager/window_manager.dart';
import 'hosting_page.dart';
import 'info_page.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WindowListener, AutomaticKeepAliveClientMixin {
  static const double _kDefaultPadding = 12.0;
  static const int _kPagesLength = 6;

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
    windowManager.show();
    windowManager.addListener(this);
    _searchController.addListener(_onSearch);
    super.initState();
  }

  void _onSearch() {
    if (searchValue.isEmpty) {
      _searchItems.value = null;
      return;
    }

    _searchItems.value = _allItems
        .whereType<PaneItem>()
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
    windowManager.getPosition()
        .then((value) => _settingsController.saveWindowOffset(value));
    super.onWindowMoved();
  }

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Stack(children: [
      LayoutBuilder(
          builder: (context, specs) => Obx(() => NavigationPaneTheme(
                data: NavigationPaneThemeData(
                    backgroundColor: FluentTheme.of(context).micaBackgroundColor.withOpacity(0.9),
                ),
                child: NavigationView(
                    paneBodyBuilder: (pane, body) => Padding(
                        padding: const EdgeInsets.all(_kDefaultPadding),
                        child: body
                    ),
                    appBar: NavigationAppBar(
                        height: 32,
                        title: _draggableArea,
                        actions: WindowTitleBar(focused: _focused()),
                        automaticallyImplyLeading: false,
                        leading: _backButton
                    ),
                    pane: NavigationPane(
                      key: appKey,
                      selected: _selectedIndex,
                      onChanged: _onIndexChanged,
                      menuButton: const SizedBox(),
                      displayMode: PaneDisplayMode.open,
                      items: _items,
                      header: ProfileWidget(),
                      footerItems: _footerItems,
                      autoSuggestBox: _autoSuggestBox,
                      autoSuggestBoxReplacement: const Icon(FluentIcons.search),
                    ),
                    contentShape: const RoundedRectangleBorder(),
                    onOpenSearch: () => _searchFocusNode.requestFocus(),
                    transitionBuilder: (child, animation) => child),
              )
          )
      ),
      if (isWin11)
        Obx(() => _focused.value ? const WindowBorder() : const SizedBox())
    ]);
  }

  GestureDetector get _draggableArea => GestureDetector(
      onDoubleTap: () async => await windowManager.isMaximized() ? await windowManager.restore() : await windowManager.maximize(),
      onHorizontalDragStart: (event) => windowManager.startDragging(),
      onVerticalDragStart: (event) => windowManager.startDragging()
  );

  Widget get _backButton => Obx(() {
    for (var entry in _navigationStatus) {
      entry.value;
    }

    var onBack = _onBack();
    return Padding(
        padding: const EdgeInsets.only(top: 8.0),
        child: Button(
            onPressed: onBack,
            style: ButtonStyle(
                backgroundColor: ButtonState.all(Colors.transparent),
                border: ButtonState.all(BorderSide(color: Colors.transparent))
            ),
            child: const Icon(FluentIcons.back, size: 13.0)
        )
    );
  });

  Function()? _onBack() {
    var navigator = _navigators[_settingsController.index.value].currentState;
    if (navigator == null || !navigator.mounted || !navigator.canPop()) {
      return null;
    }

    var status = _navigationStatus[_settingsController.index.value];
    if (status.value <= 0) {
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

  Widget get _autoSuggestBox => Padding(
    padding: const EdgeInsets.symmetric(horizontal: 8.0),
    child: TextBox(
        key: _searchKey,
        controller: _searchController,
        placeholder: 'Find a setting',
        focusNode: _searchFocusNode,
        autofocus: true
    ),
  );

  int? get _selectedIndex {
    var searchItems = _searchItems();
    if (searchItems == null) {
      return _settingsController.index();
    }

    if (_settingsController.index() >= _allItems.length) {
      return null;
    }

    var indexOnScreen =
        searchItems.indexOf(_allItems[_settingsController.index()]);
    if (indexOnScreen.isNegative) {
      return null;
    }

    return indexOnScreen;
  }

  List<NavigationPaneItem> get _allItems => [..._items, ..._footerItems];

  List<NavigationPaneItem> get _footerItems => searchValue.isNotEmpty ? [] : [
    PaneItem(
        title: const Text("Downloads"),
        icon: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8.0),
            child: SizedBox.square(
                dimension: 24,
                child: Image.asset("assets/images/download.png")
            )
        ),
        body: const SettingsPage()
    ),
    PaneItem(
        title: const Text("Settings"),
        icon: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8.0),
            child: SizedBox.square(
                dimension: 24,
                child: Image.asset("assets/images/settings.png")
            )
        ),
        body: const SettingsPage()
    )
  ];

  List<NavigationPaneItem> get _items => _searchItems() ?? [
    PaneItem(
        title: const Text("Play"),
        icon: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8.0),
            child: SizedBox.square(
                dimension: 24,
                child: Image.asset("assets/images/play.png")
            )
        ),
        body: LauncherPage(_navigators[0], _navigationStatus[0])
    ),
    PaneItem(
        title: const Text("Host"),
        icon: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8.0),
            child: SizedBox.square(
                dimension: 24,
                child: Image.asset("assets/images/host.png")
            )
        ),
        body: HostingPage(_navigators[1], _navigationStatus[1])
    ),
    PaneItem(
        title: const Text("Authenticator"),
        icon: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8.0),
            child: SizedBox.square(
                dimension: 24,
                child: Image.asset("assets/images/cloud.png")
            )
        ),
        body: ServerPage(_navigators[2], _navigationStatus[2])
    ),
    PaneItem(
        title: const Text("Tutorial"),
        icon: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8.0),
          child: SizedBox.square(
              dimension: 24,
              child: Image.asset("assets/images/info.png")
          )
        ),
        body: InfoPage(_navigators[3], _navigationStatus[3])
    )
  ];

  String get searchValue => _searchController.text;
}
