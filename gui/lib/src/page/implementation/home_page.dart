import 'dart:async';
import 'dart:io';
import 'dart:ui';

import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart' show MaterialPage;
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/controller/update_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/implementation/dll.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_suggestion.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/dll.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/info_bar_area.dart';
import 'package:reboot_launcher/src/widget/profile_tile.dart';
import 'package:reboot_launcher/src/widget/title_bar.dart';
import 'package:window_manager/window_manager.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WindowListener, AutomaticKeepAliveClientMixin {
  static const double _kDefaultPadding = 12.0;

  final SettingsController _settingsController = Get.find<SettingsController>();
  final UpdateController _updateController = Get.find<UpdateController>();
  final GlobalKey _searchKey = GlobalKey();
  final FocusNode _searchFocusNode = FocusNode();
  final TextEditingController _searchController = TextEditingController();
  final RxBool _focused = RxBool(true);

  @override
  bool get wantKeepAlive => true;

  @override
  void initState() {
    windowManager.addListener(this);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _updateController.notifyLauncherUpdate();
      _updateController.updateReboot();
      watchDlls().listen((filePath) => showDllDeletedDialog(() {
        downloadCriticalDllInteractive(filePath);
      }));
    });
    super.initState();
  }

  @override
  void onWindowClose() {
    exit(0); // Force closing
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
    _focused.value = false;
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
  Widget build(BuildContext context) {
    super.build(context);
    _settingsController.language.value;
    loadTranslations(context);
    return Obx(() => NavigationPaneTheme(
        data: NavigationPaneThemeData(
          backgroundColor: FluentTheme.of(context).micaBackgroundColor.withOpacity(0.93),
        ),
        child: NavigationView(
            paneBodyBuilder: (pane, body) => _PaneBody(
                padding: _kDefaultPadding,
                controller: pagesController,
                body: body
            ),
            appBar: NavigationAppBar(
              height: 32,
              title: _draggableArea,
              actions: WindowTitleBar(focused: _focused()),
              leading: _backButton,
              automaticallyImplyLeading: false,
            ),
            pane: NavigationPane(
                selected: pageIndex.value,
                onChanged: (index) {
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
                menuButton: const SizedBox(),
                displayMode: PaneDisplayMode.open,
                items: _items,
                customPane: _CustomPane(_settingsController),
                header: const ProfileWidget(),
                autoSuggestBox: _autoSuggestBox,
                indicator: const StickyNavigationIndicator(
                    duration: Duration(milliseconds: 500),
                    curve: Curves.easeOut,
                    indicatorSize: 3.25
                )
            ),
            contentShape: const RoundedRectangleBorder(),
            onOpenSearch: () => _searchFocusNode.requestFocus(),
            transitionBuilder: (child, animation) => child
        )
    ),
    );
  }

  Widget get _backButton => StreamBuilder(
      stream: pagesController.stream,
      builder: (context, _) => Button(
          style: ButtonStyle(
              padding: ButtonState.all(const EdgeInsets.only(top: 6.0)),
              backgroundColor: ButtonState.all(Colors.transparent),
              shape: ButtonState.all(Border())
          ),
          onPressed: appStack.isEmpty && !inDialog ? null : () {
            if(inDialog) {
              Navigator.of(appKey.currentContext!).pop();
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
      onHorizontalDragStart: (_) => appWindow.startDragging(),
      onVerticalDragStart: (_) => appWindow.startDragging()
  );

  Widget get _autoSuggestBox => Obx(() {
    final firstRun = _settingsController.firstRun.value;
    return Padding(
        padding: const EdgeInsets.symmetric(
            horizontal: 16.0,
            vertical: 8.0
        ),
        child: AutoSuggestBox<PageSuggestion>(
          key: _searchKey,
          controller: _searchController,
          enabled: !firstRun,
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
  });

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

  List<NavigationPaneItem> get _items => pages.map((page) => _createItem(page)).toList();

  NavigationPaneItem _createItem(RebootPage page) => PaneItem(
      title: Text(page.name),
      icon: SizedBox.square(
          dimension: 24,
          child: Image.asset(page.iconAsset)
      ),
      body: page
  );
}

class _PaneBody extends StatefulWidget {
  const _PaneBody({
    required this.padding,
    required this.controller,
    required this.body
  });

  final double padding;
  final StreamController<void> controller;
  final Widget? body;

  @override
  State<_PaneBody> createState() => _PaneBodyState();
}

class _PaneBodyState extends State<_PaneBody> with AutomaticKeepAliveClientMixin {
  final SettingsController _settingsController = Get.find<SettingsController>();
  final PageController _pageController = PageController(keepPage: true);

  @override
  bool get wantKeepAlive => true;

  @override
  void initState() {
    super.initState();
    pageIndex.listen((index) => _pageController.jumpToPage(index));
  }

  @override
  Widget build(BuildContext context) {
    super.build(context);
    final themeMode = _settingsController.themeMode.value;
    final inactiveColor = themeMode == ThemeMode.dark
        || (themeMode == ThemeMode.system && isDarkMode) ? Colors.grey[60] : Colors.grey[100];
    return Padding(
        padding: EdgeInsets.only(
            left: widget.padding,
            right: widget.padding * 2,
            top: widget.padding,
            bottom: widget.padding * 2
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
                      Align(
                        alignment: Alignment.centerLeft,
                        child: StreamBuilder(
                            stream: widget.controller.stream,
                            builder: (context, _) {
                              final elements = <TextSpan>[];
                              elements.add(TextSpan(
                                  text: pages[pageIndex.value].name,
                                  recognizer: pageStack.isNotEmpty ? (TapGestureRecognizer()..onTap = () {
                                    for(var i = 0; i < pageStack.length; i++) {
                                      Navigator.of(pageKey.currentContext!).pop();
                                      final element = pageStack.removeLast();
                                      appStack.remove(element);
                                    }

                                    widget.controller.add(null);
                                  }) : null,
                                  style: TextStyle(
                                      color: pageStack.isNotEmpty ? inactiveColor : null
                                  )
                              ));
                              for(var i = pageStack.length - 1; i >= 0; i--) {
                                var innerPage = pageStack.elementAt(i);
                                innerPage = innerPage.substring(innerPage.indexOf("_") + 1);
                                elements.add(TextSpan(
                                    text: " > ",
                                    style: TextStyle(
                                        color: inactiveColor
                                    )
                                ));
                                elements.add(TextSpan(
                                    text: innerPage,
                                    recognizer: i == pageStack.length - 1 ? null : (TapGestureRecognizer()..onTap = () {
                                      for(var j = 0; j < i - 1; j++) {
                                        Navigator.of(pageKey.currentContext!).pop();
                                        final element = pageStack.removeLast();
                                        appStack.remove(element);
                                      }
                                      widget.controller.add(null);
                                    }),
                                    style: TextStyle(
                                        color: i == pageStack.length - 1 ? null : inactiveColor
                                    )
                                ));
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
                      ),
                      const SizedBox(height: 24.0),
                      Expanded(
                          child: Stack(
                            fit: StackFit.loose,
                            children: [
                              PageView.builder(
                                  controller: _pageController,
                                  itemBuilder: (context, index) => Navigator(
                                    onPopPage: (page, data) => true,
                                    observers: [
                                      _NestedPageObserver(
                                          onChanged: (routeName) {
                                            if(routeName != null) {
                                              pageIndex.refresh();
                                              addSubPageToStack(routeName);
                                              widget.controller.add(null);
                                            }
                                          }
                                      )
                                    ],
                                    pages: [
                                      MaterialPage(
                                          child: KeyedSubtree(
                                              key: getPageKeyByIndex(index),
                                              child: widget.body ?? const SizedBox.shrink()
                                          )
                                      )
                                    ],
                                  ),
                                  itemCount: pages.length
                              ),
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
    );
  }
}

class _CustomPane extends NavigationPaneWidget {
  final SettingsController settingsController;
  _CustomPane(this.settingsController);

  @override
  Widget build(BuildContext context, NavigationPaneWidgetData data) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      data.appBar,
      Expanded(
        child: Navigator(
          key: appKey,
          onPopPage: (page, data) => false,
          pages: [
            MaterialPage(
                child: Row(
                  children: [
                    SizedBox(
                      width: 310,
                      child: Column(
                        mainAxisSize: MainAxisSize.max,
                        children: [
                          data.pane.header ?? const SizedBox.shrink(),
                          data.pane.autoSuggestBox ?? const SizedBox.shrink(),
                          const SizedBox(height: 12.0),
                          Expanded(
                            child: Padding(
                              padding: const EdgeInsets.symmetric(
                                  horizontal: 16.0
                              ),
                              child: Scrollbar(
                                controller: data.scrollController,
                                child: ListView.separated(
                                  controller: data.scrollController,
                                  itemCount: data.pane.items.length,
                                  separatorBuilder: (context, index) => const SizedBox(
                                      height: 4.0
                                  ),
                                  itemBuilder: (context, index) {
                                    final item = data.pane.items[index] as PaneItem;
                                    return Obx(() {
                                      final firstRun = settingsController.firstRun.value;
                                      return HoverButton(
                                        onPressed: firstRun ? null : () => data.pane.onChanged?.call(index),
                                        builder: (context, states) => Container(
                                          height: 36,
                                          decoration: BoxDecoration(
                                              color: ButtonThemeData.uncheckedInputColor(
                                                FluentTheme.of(context),
                                                item == data.pane.selectedItem ? {ButtonStates.hovering} : states,
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
                                                data.pane.indicator ?? const SizedBox.shrink(),
                                                item.icon,
                                                const SizedBox(width: 12.0),
                                                item.title ?? const SizedBox.shrink()
                                              ],
                                            ),
                                          ),
                                        ),
                                      );
                                    });
                                  },
                                ),
                              ),
                            ),
                          )
                        ],
                      ),
                    ),
                    Expanded(
                        child: data.content
                    )
                  ],
                )
            )
          ],
        ),

      )
    ],
  );
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