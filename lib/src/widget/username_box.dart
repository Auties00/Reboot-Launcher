import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/widget/smart_input.dart';

import 'package:reboot_launcher/src/controller/game_controller.dart';

class UsernameBox extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();

  UsernameBox({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Obx(() => SmartInput(
        label: "Username",
        placeholder: "Type your ${_gameController.host.value ? 'hosting' : "in-game"} username",
        controller: _gameController.username,
        populate: true
    ));
  }
}
