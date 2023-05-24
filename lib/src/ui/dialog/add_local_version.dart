import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';

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

            TextFormBox(
                controller: _nameController,
                header: "Name",
                placeholder: "Type the version's name",
                autofocus: true,
                validator: (text) => checkVersion(text, _gameController.versions.value)
            ),

            FileSelector(
                label: "Path",
                placeholder: "Type the game folder",
                windowTitle: "Select game folder",
                controller: _gamePathController,
                validator: checkGameFolder,
                folder: true
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
