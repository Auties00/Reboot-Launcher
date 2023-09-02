import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';

class MatchmakerController extends ServerController {
  late final TextEditingController gameServerAddress;

  MatchmakerController() : super() {
    gameServerAddress = TextEditingController(text: storage.read("game_server_address") ?? kDefaultMatchmakerHost);
    gameServerAddress.addListener(() => storage.write("game_server_address", gameServerAddress.text));
  }

  @override
  String get controllerName => "matchmaker";

  @override
  String get storageName => "reboot_matchmaker";

  @override
  String get defaultHost => kDefaultMatchmakerHost;

  @override
  String get defaultPort => kDefaultMatchmakerPort;

  @override
  Future<bool> get isPortFree => isMatchmakerPortFree();

  @override
  Future<bool> freePort() => freeMatchmakerPort();
}