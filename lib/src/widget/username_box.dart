import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/widget/smart_input.dart';

class UsernameBox extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();

  UsernameBox({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Obx(() => Tooltip(
      message:  _gameController.type.value != GameType.client ? "The username of the game hoster" : "The in-game username of your player",
      child: SmartInput(
          label: "Username",
          placeholder: "Type your ${_gameController.type.value != GameType.client ? 'hosting' : "in-game"} username",
          controller: _gameController.username,
          populate: true
      ),
    ));
  }
}
