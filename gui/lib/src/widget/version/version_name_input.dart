import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';

class VersionNameInput extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();
  final TextEditingController controller;

  VersionNameInput({Key? key, required this.controller}) : super(key: key);

  @override
  Widget build(BuildContext context) => InfoLabel(
    label: "Name",
    child: TextFormBox(
        controller: controller,
        placeholder: "Type the version's name",
        autofocus: true,
        validator: _validate,
        autovalidateMode: AutovalidateMode.onUserInteraction
    ),
  );

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
