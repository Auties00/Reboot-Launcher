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

GlobalKey appKey = GlobalKey();
GlobalKey get pageKey {
  var index = pageIndex.value;
  var key = _pageKeys[index];
  if(key != null) {
    return key;
  }

  var result = GlobalKey();
  _pageKeys[index] = result;
  return result;
}

List<int> get pagesWithButtonIndexes => pages.where((page) => page.hasButton)
    .map((page) => page.index)
    .toList();