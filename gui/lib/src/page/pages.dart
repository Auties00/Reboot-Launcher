import 'dart:async';
import 'dart:collection';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get_rx/src/rx_types/rx_types.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/implementation/authenticator_page.dart';
import 'package:reboot_launcher/src/page/implementation/info_page.dart';
import 'package:reboot_launcher/src/page/implementation/matchmaker_page.dart';
import 'package:reboot_launcher/src/page/implementation/play_page.dart';
import 'package:reboot_launcher/src/page/implementation/server_browser_page.dart';
import 'package:reboot_launcher/src/page/implementation/server_host_page.dart';
import 'package:reboot_launcher/src/page/implementation/settings_page.dart';

final StreamController<void> pagesController = StreamController.broadcast();
bool hitBack = false;

final List<RebootPage> pages = [
  const PlayPage(),
  const HostPage(),
  const BrowsePage(),
  const AuthenticatorPage(),
  const MatchmakerPage(),
  const InfoPage(),
  const SettingsPage()
];

final RxInt pageIndex = RxInt(0);

final HashMap<int, GlobalKey> _pageKeys = HashMap();

final GlobalKey appKey = GlobalKey();

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
