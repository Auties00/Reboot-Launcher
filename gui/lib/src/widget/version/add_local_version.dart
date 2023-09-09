import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/widget/common/file_selector.dart';
import 'package:reboot_launcher/src/widget/version/version_name_input.dart';

class AddLocalVersion extends StatefulWidget {
  const AddLocalVersion({Key? key})
      : super(key: key);

  @override
  State<AddLocalVersion> createState() => _AddLocalVersionState();
}

class _AddLocalVersionState extends State<AddLocalVersion> {
  final GameController _gameController = Get.find<GameController>();
  final TextEditingController _nameController = TextEditingController();
  final TextEditingController _gamePathController = TextEditingController();

  @override
  void initState() {
    _gamePathController.addListener(() async {
      var file = Directory(_gamePathController.text);
      if(await file.exists()) {
        if(_nameController.text.isEmpty) {
          var text = path.basename(_gamePathController.text);
          _nameController.text = text;
          _nameController.selection = TextSelection.collapsed(offset: text.length);
        }
      }
    });
    super.initState();
  }

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
                label: "Game folder",
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
              WidgetsBinding.instance.addPostFrameCallback((_) => _gameController.addVersion(FortniteVersion(
                  name: _nameController.text,
                  location: Directory(_gamePathController.text)
              )));
            },
          )
        ]
    );
  }
}
