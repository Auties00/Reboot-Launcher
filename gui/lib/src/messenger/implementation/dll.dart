import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/messenger/abstract/dialog.dart';
import 'package:reboot_launcher/src/util/translations.dart';

Future<void> showDllDeletedDialog(Function() onConfirm) => showRebootDialog(
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
            onConfirm();
          },
        ),
      ],
    )
);