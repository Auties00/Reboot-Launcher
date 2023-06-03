import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/ui/widget/home/version_name_input.dart';

import '../../util/checks.dart';
import '../widget/shared/file_selector.dart';
import '../widget/shared/smart_check_box.dart';
import 'dialog.dart';
import 'dialog_button.dart';

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
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const SizedBox(
              width: double.infinity,
              child: InfoBar(
                  title: Text("Local builds are not guaranteed to work"),
                  severity: InfoBarSeverity.info
              ),
            ),

            const SizedBox(
                height: 16.0
            ),

            VersionNameInput(
                controller: _nameController
            ),

            const SizedBox(
                height: 16.0
            ),

            FileSelector(
                label: "Path",
                placeholder: "Type the game folder",
                windowTitle: "Select game folder",
                controller: _gamePathController,
                validator: checkGameFolder,
                folder: true
            ),

            const SizedBox(
                height: 16.0
            )
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
              Navigator.of(context).pop();
              _gameController.addVersion(FortniteVersion(
                  name: _nameController.text,
                  location: Directory(_gamePathController.text)
              ));
            },
          )
        ]
    );
  }
}
