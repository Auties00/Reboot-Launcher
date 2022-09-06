import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/widget/select_file.dart';

import 'package:reboot_launcher/src/model/fortnite_version.dart';

class AddLocalVersion extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();
  final TextEditingController _nameController = TextEditingController();
  final TextEditingController _gamePathController = TextEditingController();

  AddLocalVersion({Key? key})
      : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Form(
        child: Builder(
            builder: (formContext) => ContentDialog(
                constraints:
                    const BoxConstraints(maxWidth: 368, maxHeight: 278),
                content: _createLocalVersionDialogBody(),
                actions: _createLocalVersionActions(formContext))));
  }

  List<Widget> _createLocalVersionActions(BuildContext context) {
    return [
      FilledButton(
        onPressed: () => _closeLocalVersionDialog(context, false),
        style: ButtonStyle(backgroundColor: ButtonState.all(Colors.red)),
        child: const Text('Close'),
      ),
      FilledButton(
          child: const Text('Save'),
          onPressed: () => _closeLocalVersionDialog(context, true))
    ];
  }

  Future<void> _closeLocalVersionDialog(BuildContext context, bool save) async {
    if (save) {
      if (!Form.of(context)!.validate()) {
        return;
      }

      _gameController.addVersion(FortniteVersion(
          name: _nameController.text,
          location: Directory(_gamePathController.text)));
    }

    Navigator.of(context).pop(save);
  }

  Widget _createLocalVersionDialogBody() {
    return Column(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        TextFormBox(
          controller: _nameController,
          header: "Name",
          placeholder: "Type the version's name",
          autofocus: true,
          validator: (text) {
            if (text == null || text.isEmpty) {
              return 'Invalid version name';
            }

            if (_gameController.versions.value.any((element) => element.name == text)) {
              return 'Existent game version';
            }

            return null;
          },
        ),
        SelectFile(
            label: "Location",
            placeholder: "Type the game folder",
            windowTitle: "Select game folder",
            controller: _gamePathController,
            validator: _checkGameFolder)
      ],
    );
  }

  String? _checkGameFolder(text) {
    if (text == null || text.isEmpty) {
      return 'Invalid game path';
    }

    var directory = Directory(text);
    if (!directory.existsSync()) {
      return "Nonexistent game path";
    }

    if (!directory.existsSync()) {
      return "Nonexistent game path";
    }

    if (!FortniteVersion.findExecutable(directory, "FortniteClient-Win64-Shipping.exe").existsSync()) {
      return "Invalid game path";
    }

    return null;
  }
}
