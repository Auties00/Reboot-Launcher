import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get_rx/src/rx_types/rx_types.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class MatchmakerController extends ServerController {
  late final TextEditingController gameServerAddress;
  late final FocusNode gameServerAddressFocusNode;
  late final RxnString gameServerOwner;

  MatchmakerController() : super() {
    gameServerAddress = TextEditingController(text: storage.read("game_server_address") ?? kDefaultMatchmakerHost);
    var lastValue = gameServerAddress.text;
    writeMatchmakingIp(lastValue);
    gameServerAddress.addListener(() {
      var newValue = gameServerAddress.text;
      if(newValue.trim().toLowerCase() == lastValue.trim().toLowerCase()) {
        return;
      }

      lastValue = newValue;
      gameServerAddress.selection = TextSelection.collapsed(offset: newValue.length);
      storage.write("game_server_address", newValue);
      writeMatchmakingIp(newValue);
    });
    watchMatchmakingIp().listen((event) {
      if(event != null && gameServerAddress.text != event) {
        gameServerAddress.text = event;
      }
    });
    gameServerAddressFocusNode = FocusNode();
    gameServerOwner = RxnString(storage.read("game_server_owner"));
    gameServerOwner.listen((value) => storage.write("game_server_owner", value));
  }

  @override
  String get controllerName => translations.matchmakerName.toLowerCase();

  @override
  String get storageName => "matchmaker";

  @override
  String get defaultHost => kDefaultMatchmakerHost;

  @override
  int get defaultPort => kDefaultMatchmakerPort;

  @override
  Future<bool> get isPortFree => isMatchmakerPortFree();

  @override
  Future<bool> freePort() => freeMatchmakerPort();

  @override
  RebootPageType get pageType => RebootPageType.matchmaker;

  @override
  Future<Process> startEmbeddedInternal() => startEmbeddedMatchmaker();

  @override
  Future<Uri?> pingServer(String host, int port) => pingMatchmaker(host, port);
}