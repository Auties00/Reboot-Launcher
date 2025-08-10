import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';

Future<void> showDllDeletedDialog() => showRebootDialog(
    builder: (context) => InfoDialog(
      text: translations.dllDeletedTitle,
      buttons: [
        DialogButton(
          type: ButtonType.secondary,
          text: translations.dllDeletedSecondaryAction,
        ),
        DialogButton(
          type: ButtonType.secondary,
          text: translations.dllDeletedPrimaryAction,
          onTap: () {
            Navigator.pop(context);

          },
        ),
      ],
    )
);