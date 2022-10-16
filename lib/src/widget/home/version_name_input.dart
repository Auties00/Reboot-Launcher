import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';

class VersionNameInput extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();
  final TextEditingController controller;

  VersionNameInput({Key? key, required this.controller}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return TextFormBox(
      header: "Name",
      placeholder: "Type the version's name",
      controller: controller,
      autofocus: true,
      validator: _validate,
    );
  }

  String? _validate(String? text) {
    if (text == null || text.isEmpty) {
      return 'Empty version name';
    }

    if (_gameController.versions.value.any((element) => element.name == text)) {
      return 'This version already exists';
    }

    return null;
  }
}
