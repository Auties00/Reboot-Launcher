import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class VersionNameInput extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();
  final TextEditingController controller;

  VersionNameInput({Key? key, required this.controller}) : super(key: key);

  @override
  Widget build(BuildContext context) => InfoLabel(
    label: translations.versionName,
    child: TextFormBox(
        controller: controller,
        placeholder: translations.versionNameLabel,
        autofocus: true,
        validator: (version) => checkVersion(version, _gameController.versions.value),
        autovalidateMode: AutovalidateMode.onUserInteraction
    ),
  );
}
