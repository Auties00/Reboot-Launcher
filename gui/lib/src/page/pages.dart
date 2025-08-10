import 'dart:async';
import 'dart:collection';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/pager/page_type.dart';
import 'package:reboot_launcher/src/messenger/overlay.dart';
import 'package:reboot_launcher/src/page/backend_page.dart';
import 'package:reboot_launcher/src/page/browser_page.dart';
import 'package:reboot_launcher/src/page/host_page.dart';
import 'package:reboot_launcher/src/page/info_page.dart';
import 'package:reboot_launcher/src/pager/abstract_page.dart';
import 'package:reboot_launcher/src/page/play_page.dart';
import 'package:reboot_launcher/src/page/settings_page.dart';
import 'package:reboot_launcher/src/messenger/info_bar_area.dart';

final StreamController<void> pagesController = StreamController.broadcast();
bool hitBack = false;

final List<AbstractPage> pages = [
  const PlayPage(),
  const HostPage(),
  const BrowsePage(),
  const BackendPage(),
  const InfoPage(),
  const SettingsPage()
];

final List<GlobalKey<OverlayTargetState>> _flyoutPageControllers = List.generate(pages.length, (_) => GlobalKey());

final RxInt pageIndex = RxInt(PageType.play.index);

final HashMap<int, GlobalKey> _pageKeys = HashMap();

final GlobalKey<NavigatorState> appNavigatorKey = GlobalKey();

final GlobalKey<OverlayState> appOverlayKey = GlobalKey();

final GlobalKey<InfoBarAreaState> infoBarAreaKey = GlobalKey();

GlobalKey get pageKey => getPageKeyByIndex(pageIndex.value);

GlobalKey getPageKeyByIndex(int index) {
  final key = _pageKeys[index];
  if(key != null) {
    return key;
  }

  final result = GlobalKey();
  _pageKeys[index] = result;
  return result;
}

bool get hasPageButton => currentPage.hasButton(currentPageStack.lastOrNull);

AbstractPage get currentPage => pages[pageIndex.value];

final Queue<Object?> appStack = _createAppStack();
Queue _createAppStack() {
  final queue = Queue();
  var lastValue = pageIndex.value;
  pageIndex.listen((index) {
    if(!hitBack && lastValue != index) {
      queue.add(lastValue);
      pagesController.add(null);
    }

    hitBack = false;
    lastValue = index;
  });
  return queue;
}

final Map<int, Queue<String>> _pagesStack = Map.fromEntries(List.generate(pages.length, (index) => MapEntry(index, Queue<String>())));

Queue<String> get currentPageStack => _pagesStack[pageIndex.value]!;

void addSubPageToCurrent(String pageName) {
  final index = pageIndex.value;
  appStack.add(pageName);
  _pagesStack[index]!.add(pageName);
  pagesController.add(null);
}

GlobalKey<OverlayTargetState> getOverlayTargetKeyByPage(int pageIndex) => _flyoutPageControllers[pageIndex];

GlobalKey<OverlayTargetState> get pageOverlayTargetKey => _flyoutPageControllers[pageIndex.value];
