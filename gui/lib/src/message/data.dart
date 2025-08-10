import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';

Future<void> showResetDialog(Function() onConfirm) => showRebootDialog(
    builder: (context) => InfoDialog(
      text: translations.resetDefaultsDialogTitle,
      buttons: [
        DialogButton(
          type: ButtonType.secondary,
          text: translations.resetDefaultsDialogSecondaryAction,
        ),
        DialogButton(
          type: ButtonType.primary,
          text: translations.resetDefaultsDialogPrimaryAction,
          onTap: () {
            onConfirm();
            Navigator.of(context).pop();
          },
        )
      ],
    )
);