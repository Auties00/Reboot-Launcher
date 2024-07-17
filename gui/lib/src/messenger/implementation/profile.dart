import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/material.dart' show Icons;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/messenger/abstract/dialog.dart';
import 'package:reboot_launcher/src/util/translations.dart';

final GameController _gameController = Get.find<GameController>();

Future<bool> showProfileForm(BuildContext context) async{
  final showPassword = RxBool(false);
  final oldUsername = _gameController.username.text;
  final showPasswordTrailing = RxBool(oldUsername.isNotEmpty);
  final oldPassword = _gameController.password.text;
  final result = await showRebootDialog<bool?>(
      builder: (context) => Obx(() => FormDialog(
          content: Column(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              InfoLabel(
                  label: translations.usernameOrEmail,
                  child: TextFormBox(
                    placeholder: translations.usernameOrEmailPlaceholder,
                    controller: _gameController.username,
                    autovalidateMode: AutovalidateMode.always,
                    enableSuggestions: true,
                    autofocus: true,
                    autocorrect: false,
                  )
              ),
              const SizedBox(height: 16.0),
              InfoLabel(
                  label: translations.password,
                  child: TextFormBox(
                      placeholder: translations.passwordPlaceholder,
                      controller: _gameController.password,
                      autovalidateMode: AutovalidateMode.always,
                      obscureText: !showPassword.value,
                      enableSuggestions: false,
                      autocorrect: false,
                      onChanged: (text) => showPasswordTrailing.value = text.isNotEmpty,
                      suffix: Button(
                        onPressed: () => showPassword.value = !showPassword.value,
                        style: ButtonStyle(
                            shape: ButtonState.all(const CircleBorder()),
                            backgroundColor: ButtonState.all(Colors.transparent)
                        ),
                        child: Icon(
                            showPassword.value ? Icons.visibility_off : Icons.visibility,
                            color: showPasswordTrailing.value ? null : Colors.transparent
                        ),
                      )
                  )
              ),
              const SizedBox(height: 8.0)
            ],
          ),
          buttons: [
            DialogButton(
                text: translations.cancelProfileChanges,
                type: ButtonType.secondary
            ),

            DialogButton(
                text: translations.saveProfileChanges,
                type: ButtonType.primary,
                onTap: () => Navigator.of(context).pop(true)
            )
          ]
      ))
  ) ?? false;
  if(result) {
    return true;
  }

  _gameController.username.text = oldUsername;
  _gameController.password.text = oldPassword;
  return false;
}
