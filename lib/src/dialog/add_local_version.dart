import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';

import '../util/checks.dart';
import '../widget/os/file_selector.dart';

class AddLocalVersion extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();
  final TextEditingController _nameController = TextEditingController();
  final TextEditingController _gamePathController = TextEditingController();

  AddLocalVersion({Key? key})
      : super(key: key);

  @override
  Widget build(BuildContext context) {
    return FormDialog(
        content: Column(
          mainAxisSize: MainAxisSize.min,
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            TextFormBox(
                controller: _nameController,
                header: "Name",
                placeholder: "Type the version's name",
                autofocus: true,
                validator: (text) => checkVersion(text, _gameController.versions.value)
            ),

            const SizedBox(
                height: 16.0
            ),

            FileSelector(
                label: "Location",
                placeholder: "Type the game folder",
                windowTitle: "Select game folder",
                controller: _gamePathController,
                validator: checkGameFolder,
                folder: true
            ),

            const SizedBox(height: 8.0),
          ],
        ),
        buttons: [
          DialogButton(
              type: ButtonType.secondary
          ),

          DialogButton(
            text: "Save",
            type: ButtonType.primary,
            onTap: () {
              _gameController.addVersion(FortniteVersion(
                  name: _nameController.text,
                  location: Directory(_gamePathController.text)));
            },
          )
        ]
    );
  }
}
