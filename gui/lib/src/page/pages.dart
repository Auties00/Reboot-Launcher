import 'dart:async';
import 'dart:collection';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/messenger/overlay.dart';
import 'package:reboot_launcher/src/page/page.dart';
import 'package:reboot_launcher/src/page/page_type.dart';
import 'package:reboot_launcher/src/widget/page/backend_page.dart';
import 'package:reboot_launcher/src/widget/page/browser_page.dart';
import 'package:reboot_launcher/src/widget/page/host_page.dart';
import 'package:reboot_launcher/src/widget/page/info_page.dart';
import 'package:reboot_launcher/src/widget/page/play_page.dart';
import 'package:reboot_launcher/src/widget/page/settings_page.dart';
import 'package:reboot_launcher/src/widget/window/info_bar_area.dart';

final StreamController<void> pagesController = StreamController.broadcast();
bool hitBack = false;

final List<RebootPage> pages = [
  const PlayPage(),
  const HostPage(),
  const BrowsePage(),
  const BackendPage(),
  const InfoPage(),
  const SettingsPage()
];

final List<GlobalKey<OverlayTargetState>> _flyoutPageControllers = List.generate(pages.length, (_) => GlobalKey());

final RxInt pageIndex = RxInt(RebootPageType.play.index);

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

bool get hasPageButton => pages[pageIndex.value].hasButton(pageStack.lastOrNull);

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

Queue<String> get pageStack => _pagesStack[pageIndex.value]!;

void addSubPageToStack(String pageName) {
  final index = pageIndex.value;
  final identifier = "${index}_$pageName";
  appStack.add(identifier);
  _pagesStack[index]!.add(identifier);
  pagesController.add(null);
}

GlobalKey<OverlayTargetState> getOverlayTargetKeyByPage(int pageIndex) => _flyoutPageControllers[pageIndex];

GlobalKey<OverlayTargetState> get pageOverlayTargetKey => _flyoutPageControllers[pageIndex.value];
