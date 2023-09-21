import 'dart:collection';
import 'dart:ui';

import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/material.dart' show MaterialPage;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_setting.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:reboot_launcher/src/widget/home/profile.dart';
import 'package:reboot_launcher/src/widget/os/title_bar.dart';
import 'package:window_manager/window_manager.dart';

class HomePage extends StatefulWidget {
  const HomePage({Key? key}) : super(key: key);

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WindowListener, AutomaticKeepAliveClientMixin {
  static const double _kDefaultPadding = 12.0;

  final SettingsController _settingsController = Get.find<SettingsController>();
  final GlobalKey _searchKey = GlobalKey();
  final FocusNode _searchFocusNode = FocusNode();
  final TextEditingController _searchController = TextEditingController();
  final RxBool _focused = RxBool(true);
  final Queue<int> _pagesStack = Queue();
  bool _hitBack = false;

  @override
  bool get wantKeepAlive => true;

  @override
  void initState() {
    windowManager.addListener(this);
    var lastValue = pageIndex.value;
    pageIndex.listen((value) {
      if(_hitBack) {
        _hitBack = false;
        return;
      }

      if(value == lastValue) {
        return;
      }

      _pagesStack.add(lastValue);
      WidgetsBinding.instance.addPostFrameCallback((_) {
        restoreMessage(value, lastValue);
        lastValue = value;
      });
    });
    super.initState();
  }

  @override
  void dispose() {
    _searchFocusNode.dispose();
    _searchController.dispose();
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
    return Obx(() {
      _settingsController.language.value;
      loadTranslations(context);
      return NavigationPaneTheme(
        data: NavigationPaneThemeData(
          backgroundColor: FluentTheme.of(context).micaBackgroundColor.withOpacity(0.93),
        ),
        child: NavigationView(
            paneBodyBuilder: (pane, body) => Navigator(
              onPopPage: (page, data) => false,
              pages: [
                MaterialPage(
                    child: Padding(
                        padding: const EdgeInsets.all(_kDefaultPadding),
                        child: SizedBox(
                            key: pageKey,
                            child: body
                        )
                    )
                )
              ],
            ),
            appBar: NavigationAppBar(
              height: 32,
              title: _draggableArea,
              actions: WindowTitleBar(focused: _focused()),
              leading: _backButton,
              automaticallyImplyLeading: false,
            ),
            pane: NavigationPane(
              key: appKey,
              selected: pageIndex.value,
              onChanged: (index) => pageIndex.value = index,
              menuButton: const SizedBox(),
              displayMode: PaneDisplayMode.open,
              items: _items,
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
    );
    });
  }

  Widget get _backButton => Obx(() {
    pageIndex.value;
    return Button(
      style: ButtonStyle(
          padding: ButtonState.all(const EdgeInsets.only(top: 6.0)),
          backgroundColor: ButtonState.all(Colors.transparent),
          border: ButtonState.all(const BorderSide(color: Colors.transparent))
      ),
      onPressed: _pagesStack.isEmpty ? null : () {
        _hitBack = true;
        pageIndex.value = _pagesStack.removeLast();
      },
      child: const Icon(FluentIcons.back, size: 12.0),
    );
  });

  GestureDetector get _draggableArea => GestureDetector(
      onDoubleTap: appWindow.maximizeOrRestore,
      onHorizontalDragStart: (_) => appWindow.startDragging(),
      onVerticalDragStart: (_) => appWindow.startDragging()
  );

  Widget get _autoSuggestBox => Padding(
      padding: const EdgeInsets.symmetric(horizontal: 8.0),
      child: AutoSuggestBox<PageSetting>(
        key: _searchKey,
        controller: _searchController,
        placeholder: translations.find,
        focusNode: _searchFocusNode,
        selectionHeightStyle: BoxHeightStyle.max,
        itemBuilder: (context, item) => Wrap(
          children: [
            ListTile(
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
                ),
                subtitle: item.value.description.isNotEmpty ? Text(
                    item.value.description,
                    overflow: TextOverflow.clip,
                    maxLines: 1
                ) : null
            ),
          ],
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

  List<AutoSuggestBoxItem<PageSetting>> get _suggestedItems => pages.mapMany((page) {
    var icon = SizedBox.square(
        dimension: 24,
        child: Image.asset(page.iconAsset)
    );
    var outerResults = <AutoSuggestBoxItem<PageSetting>>[];
    outerResults.add(AutoSuggestBoxItem(
        value: PageSetting(
            name: page.name,
            description: "",
            pageIndex: page.index
        ),
        label: page.name,
        child: icon
    ));
    outerResults.addAll(page.settings.mapMany((setting) {
      var results = <AutoSuggestBoxItem<PageSetting>>[];
      results.add(AutoSuggestBoxItem(
          value: setting.withPageIndex(page.index),
          label: setting.toString(),
          child: icon
      ));
      setting.children?.forEach((childSetting) => results.add(AutoSuggestBoxItem(
          value: childSetting.withPageIndex(page.index),
          label: childSetting.toString(),
          child: icon
      )));
      return results;
    }).toList());
    return outerResults;
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