import 'dart:async';
import 'dart:io';
import 'dart:ui';

import 'package:app_links/app_links.dart';
import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart' show MaterialPage;
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/messenger/abstract/dialog.dart';
import 'package:reboot_launcher/src/messenger/abstract/info_bar.dart';
import 'package:reboot_launcher/src/messenger/abstract/overlay.dart';
import 'package:reboot_launcher/src/messenger/implementation/dll.dart';
import 'package:reboot_launcher/src/messenger/implementation/server.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_suggestion.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/matchmaker.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/info_bar_area.dart';
import 'package:reboot_launcher/src/widget/profile_tile.dart';
import 'package:reboot_launcher/src/widget/title_bar.dart';
import 'package:window_manager/window_manager.dart';

final GlobalKey<OverlayTargetState> profileOverlayKey = GlobalKey();

class HomePage extends StatefulWidget {
  static const double kDefaultPadding = 12.0;
  static const double kTitleBarHeight = 32;

  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WindowListener, AutomaticKeepAliveClientMixin {
  final BackendController _backendController = Get.find<BackendController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final SettingsController _settingsController = Get.find<SettingsController>();
  final GlobalKey _searchKey = GlobalKey();
  final FocusNode _searchFocusNode = FocusNode();
  final TextEditingController _searchController = TextEditingController();
  final RxBool _focused = RxBool(true);
  final PageController _pageController = PageController(keepPage: true, initialPage: pageIndex.value);

  @override
  bool get wantKeepAlive => true;

  @override
  void initState() {
    super.initState();
    windowManager.setPreventClose(true);
    windowManager.addListener(this);
    _syncPageViewWithNavigator();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _checkUpdates();
      _initAppLink();
      _checkGameServer();
    });
  }

  void _syncPageViewWithNavigator() {
    var lastPage = pageIndex.value;
    pageIndex.listen((index) {
      if(index == lastPage) {
        return;
      }

      lastPage = index;
      _pageController.jumpToPage(index);
    });
  }

  void _initAppLink() async {
    final appLinks = AppLinks();
    final initialUrl = await appLinks.getInitialLink();
    if(initialUrl != null) {
      _joinServer(initialUrl);
    }

    appLinks.uriLinkStream.listen(_joinServer);
  }

  void _joinServer(Uri uri) {
    final uuid = uri.host;
    final server = _hostingController.findServerById(uuid);
    if(server != null) {
      _backendController.joinServerInteractive(_hostingController.uuid, server);
    }else {
      showRebootInfoBar(
          translations.noServerFound,
          duration: infoBarLongDuration,
          severity: InfoBarSeverity.error
      );
    }
  }

  Future<void> _checkGameServer() async {
    try {
      final address = _backendController.gameServerAddress.text;
      if(isLocalHost(address)) {
        return;
      }

      var result = await pingGameServer(address);
      if(result) {
        return;
      }

      _backendController.joinLocalhost();
      WidgetsBinding.instance.addPostFrameCallback((_) => showRebootInfoBar(
          translations.serverNoLongerAvailableUnnamed,
          severity: InfoBarSeverity.warning,
          duration: infoBarLongDuration
      ));
    }catch(_) {
      // Intended behaviour
      // Just ignore the error
    }
  }

  void _checkUpdates() {
    _settingsController.notifyLauncherUpdate();

    if(!dllsDirectory.existsSync()) {
      dllsDirectory.createSync(recursive: true);
    }

    for(final injectable in InjectableDll.values) {
      final (file, custom) = _settingsController.getInjectableData(injectable);
      if(!custom) {
        _settingsController.downloadCriticalDllInteractive(
            file.path,
            silent: true
        );
      }
    }

    watchDlls().listen((filePath) => showDllDeletedDialog(() {
      _settingsController.downloadCriticalDllInteractive(filePath);
    }));
  }

  @override
  void onWindowClose() async {
    try {
      await _hostingController.discardServer();
    }finally {
      exit(0); // Force closing
    }
  }

  @override
  void dispose() {
    _searchFocusNode.dispose();
    _searchController.dispose();
    pagesController.close();
    windowManager.removeListener(this);
    super.dispose();
  }

  @override
  void onWindowFocus() {
    _focused.value = true;
  }

  @override
  void onWindowBlur() {
    _focused.value = !_focused.value;
  }

  @override
  void onWindowDocked() {
    _focused.value = true;
  }

  @override
  void onWindowMaximize() {
    _focused.value = true;
  }

  @override
  void onWindowMinimize() {
    _focused.value = false;
  }

  @override
  void onWindowResize() {
    _focused.value = true;
  }

  @override
  void onWindowMove() {
    _focused.value = true;
  }

  @override
  void onWindowRestore() {
    _focused.value = true;
  }

  @override
  void onWindowUndocked() {
    _focused.value = true;
  }

  @override
  void onWindowUnmaximize() {
    _focused.value = true;
  }

  @override
  void onWindowResized() {
    _settingsController.saveWindowSize(appWindow.size);
    _focused.value = true;
  }

  @override
  void onWindowMoved() {
    _settingsController.saveWindowOffset(appWindow.position);
    _focused.value = true;
  }

  @override
  void onWindowEnterFullScreen() {
    _focused.value = true;
  }

  @override
  void onWindowLeaveFullScreen() {
    _focused.value = true;
  }

  @override
  void onWindowEvent(String eventName) {
    if(eventName != "move") {
      WidgetsBinding.instance.addPostFrameCallback((_) => log("[WINDOW] Event: $eventName ${_focused.value}"));
    }
  }

  @override
  Widget build(BuildContext context) {
    super.build(context);
    _settingsController.language.value;
    loadTranslations(context);
    return Obx(() {
      return Container(
        color: FluentTheme.of(context).micaBackgroundColor.withOpacity(0.93),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            SizedBox(
              height: HomePage.kTitleBarHeight,
              child: Row(
                children: [
                  _backButton,
                  Expanded(child: _draggableArea),
                  WindowTitleBar(focused: _focused())
                ],
              )
            ),
            Expanded(
              child: Navigator(
                key: appNavigatorKey,
                onPopPage: (page, data) => false,
                pages: [
                  MaterialPage(
                    child: Overlay(
                      key: appOverlayKey,
                      initialEntries: [
                        OverlayEntry(
                            maintainState: true,
                            builder: (context) => Row(
                              children: [
                                _buildLateralView(),
                                _buildBody()
                              ],
                            )
                        )
                      ],
                    ),
                  )
                ],
              )
            )
          ],
        ),
      );
    });
  }

  Widget _buildBody() {
    return Expanded(
      child: Padding(
          padding: EdgeInsets.only(
              left: HomePage.kDefaultPadding,
              right: HomePage.kDefaultPadding * 2,
              top: HomePage.kDefaultPadding,
              bottom: HomePage.kDefaultPadding * 2
          ),
          child: Column(
            children: [
              Expanded(
                child: ConstrainedBox(
                  constraints: BoxConstraints(
                      maxWidth: 1000
                  ),
                  child: Center(
                    child: Column(
                      children: [
                        _buildBodyHeader(),
                        const SizedBox(height: 24.0),
                        Expanded(
                            child: Stack(
                              fit: StackFit.loose,
                              children: [
                                _buildBodyContent(),
                                InfoBarArea(
                                    key: infoBarAreaKey
                                )
                              ],
                            )
                        ),
                      ],
                    ),
                  ),
                ),
              )
            ],
          )
      ),
    );
  }

  Widget _buildBodyContent() => PageView.builder(
      controller: _pageController,
      itemBuilder: (context, index) => Navigator(
        onPopPage: (page, data) => true,
        observers: [
          _NestedPageObserver(
              onChanged: (routeName) {
                if(routeName != null) {
                  pageIndex.refresh();
                  addSubPageToStack(routeName);
                  pagesController.add(null);
                }
              }
          )
        ],
        pages: [
          MaterialPage(
              child: KeyedSubtree(
                  key: getPageKeyByIndex(index),
                  child: pages[index]
              )
          )
        ],
      ),
      itemCount: pages.length
  );

  Widget _buildBodyHeader() {
    final themeMode = _settingsController.themeMode.value;
    final inactiveColor = themeMode == ThemeMode.dark
        || (themeMode == ThemeMode.system && isDarkMode) ? Colors.grey[60] : Colors.grey[100];
    return Align(
      alignment: Alignment.centerLeft,
      child: StreamBuilder(
          stream: pagesController.stream,
          builder: (context, _) {
            final elements = <TextSpan>[];
            elements.add(_buildBodyHeaderRootPage(inactiveColor));
            for(var i = pageStack.length - 1; i >= 0; i--) {
              var innerPage = pageStack.elementAt(i);
              innerPage = innerPage.substring(innerPage.indexOf("_") + 1);
              elements.add(_buildBodyHeaderPageSeparator(inactiveColor));
              elements.add(_buildBodyHeaderNestedPage(innerPage, i, inactiveColor));
            }

            return Text.rich(
              TextSpan(
                  children: elements
              ),
              style: TextStyle(
                  fontSize: 32.0,
                  fontWeight: FontWeight.w600
              ),
            );
          }
      ),
    );
  }

  TextSpan _buildBodyHeaderRootPage(Color inactiveColor) => TextSpan(
      text: pages[pageIndex.value].name,
      recognizer: pageStack.isNotEmpty ? (TapGestureRecognizer()..onTap = () {
        if(inDialog) {
          return;
        }

        for(var i = 0; i < pageStack.length; i++) {
          Navigator.of(pageKey.currentContext!).pop();
          final element = pageStack.removeLast();
          appStack.remove(element);
        }

        pagesController.add(null);
      }) : null,
      style: TextStyle(
          color: pageStack.isNotEmpty ? inactiveColor : null
      )
  );

  TextSpan _buildBodyHeaderPageSeparator(Color inactiveColor) => TextSpan(
      text: " > ",
      style: TextStyle(
          color: inactiveColor
      )
  );

  TextSpan _buildBodyHeaderNestedPage(String nestedPageName, int nestedPageIndex, Color inactiveColor) => TextSpan(
      text: nestedPageName,
      recognizer: nestedPageIndex == pageStack.length - 1 ? null : (TapGestureRecognizer()..onTap = () {
        if(inDialog) {
          return;
        }

        for(var j = 0; j < nestedPageIndex - 1; j++) {
          Navigator.of(pageKey.currentContext!).pop();
          final element = pageStack.removeLast();
          appStack.remove(element);
        }
        pagesController.add(null);
      }),
      style: TextStyle(
          color: nestedPageIndex == pageStack.length - 1 ? null : inactiveColor
      )
  );

  Widget _buildLateralView() => SizedBox(
    width: 310,
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        ProfileWidget(
          overlayKey: profileOverlayKey
        ),
        _autoSuggestBox,
        const SizedBox(height: 12.0),
        _buildNavigationTrail()
      ],
    ),
  );

  Widget _buildNavigationTrail() => Expanded(
      child: Padding(
        padding: const EdgeInsets.symmetric(
            horizontal: 16.0
        ),
        child: Scrollbar(
          child: ListView.separated(
            primary: true,
            itemCount: pages.length,
            separatorBuilder: (context, index) => const SizedBox(
                height: 4.0
            ),
            itemBuilder: (context, index) => _buildNavigationItem(pages[index]),
          ),
        ),
      )
  );

  Widget _buildNavigationItem(RebootPage page) {
    final index = page.type.index;
    return OverlayTarget(
      key: getOverlayTargetKeyByPage(index),
      child: HoverButton(
        onPressed: () {
          final lastPageIndex = pageIndex.value;
          if(lastPageIndex != index) {
            pageIndex.value = index;
          }else if(pageStack.isNotEmpty) {
            Navigator.of(pageKey.currentContext!).pop();
            final element = pageStack.removeLast();
            appStack.remove(element);
            pagesController.add(null);
          }
        },
        builder: (context, states) => Obx(() => Container(
          height: 36,
          decoration: BoxDecoration(
              color: ButtonThemeData.uncheckedInputColor(
                FluentTheme.of(context),
                pageIndex.value == index ? {ButtonStates.hovering} : states,
                transparentWhenNone: true,
              ),
              borderRadius: BorderRadius.all(Radius.circular(6.0))
          ),
          child: Padding(
            padding: const EdgeInsets.symmetric(
                horizontal: 8.0
            ),
            child: Row(
              children: [
                SizedBox.square(
                    dimension: 24,
                    child: Image.asset(page.iconAsset)
                ),
                const SizedBox(width: 12.0),
                Text(page.name)
              ],
            ),
          ),
        )),
      ),
    );
  }

  Widget get _backButton => StreamBuilder(
      stream: pagesController.stream,
      builder: (context, _) => Button(
        style: ButtonStyle(
            padding: ButtonState.all(const EdgeInsets.symmetric(
              vertical: 12.0,
              horizontal: 16.0
            )),
            backgroundColor: ButtonState.all(Colors.transparent),
            shape: ButtonState.all(Border())
        ),
        onPressed: appStack.isEmpty && !inDialog ? null : () {
          if(inDialog) {
            Navigator.of(appNavigatorKey.currentContext!).pop();
          }else {
            final lastPage = appStack.removeLast();
            pageStack.remove(lastPage);
            if (lastPage is int) {
              hitBack = true;
              pageIndex.value = lastPage;
            } else {
              Navigator.of(pageKey.currentContext!).pop();
            }
          }
          pagesController.add(null);
        },
        child: const Icon(FluentIcons.back, size: 12.0),
      )
  );

  GestureDetector get _draggableArea => GestureDetector(
      onDoubleTap: appWindow.maximizeOrRestore,
      onHorizontalDragStart: (_) => windowManager.startDragging(),
      onVerticalDragStart: (_) => windowManager.startDragging()
  );

  Widget get _autoSuggestBox => Padding(
      padding: const EdgeInsets.symmetric(
          horizontal: 16.0,
          vertical: 8.0
      ),
      child: AutoSuggestBox<PageSuggestion>(
        key: _searchKey,
        controller: _searchController,
        placeholder: translations.find,
        focusNode: _searchFocusNode,
        selectionHeightStyle: BoxHeightStyle.max,
        itemBuilder: (context, item) => ListTile(
            onPressed: () {
              pageIndex.value = item.value.pageIndex;
              _searchController.clear();
              _searchFocusNode.unfocus();
            },
            leading: item.child,
            title: Text(
                item.value.name,
                overflow: TextOverflow.clip,
                maxLines: 1
            )
        ),
        items: _suggestedItems,
        autofocus: true,
        trailingIcon: IgnorePointer(
            child: IconButton(
              onPressed: () {},
              icon: Transform.flip(
                  flipX: true,
                  child: const Icon(FluentIcons.search)
              ),
            )
        ),
      )
  );

  List<AutoSuggestBoxItem<PageSuggestion>> get _suggestedItems => pages.mapMany((page) {
    final pageIcon = SizedBox.square(
        dimension: 24,
        child: Image.asset(page.iconAsset)
    );
    final results = <AutoSuggestBoxItem<PageSuggestion>>[];
    results.add(AutoSuggestBoxItem(
        value: PageSuggestion(
            name: page.name,
            description: "",
            pageIndex: page.index
        ),
        label: page.name,
        child: pageIcon
    ));
    return results;
  }).toList();
}

class _NestedPageObserver extends NavigatorObserver {
  final void Function(String?) onChanged;

  _NestedPageObserver({required this.onChanged});

  @override
  void didPush(Route route, Route? previousRoute) {
    if(previousRoute != null) {
      WidgetsBinding.instance.addPostFrameCallback((_) => onChanged(route.settings.name));
    }
  }
}