import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/util/translations.dart';

Future<void> showDllDeletedDialog(Function() onConfirm) => showAppDialog(
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