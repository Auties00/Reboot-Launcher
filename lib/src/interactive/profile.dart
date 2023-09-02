import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:flutter/material.dart' show Icons;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';

final GameController _gameController = Get.find<GameController>();

Future<bool> showProfileForm(BuildContext context) async{
  var showPassword = RxBool(false);
  var oldUsername = _gameController.username.text;
  var showPasswordTrailing = RxBool(oldUsername.isNotEmpty);
  var oldPassword = _gameController.password.text;
  var result = await showDialog<bool?>(
      builder: (context) => Obx(() => FormDialog(
          content: Column(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              InfoLabel(
                  label: "Username/Email",
                  child: TextFormBox(
                    placeholder: "Type your username or email",
                    controller: _gameController.username,
                    autovalidateMode: AutovalidateMode.always,
                    enableSuggestions: true,
                    autofocus: true,
                    autocorrect: false,
                  )
              ),
              const SizedBox(height: 16.0),
              InfoLabel(
                  label: "Password",
                  child: TextFormBox(
                      placeholder: "Type your password, if you have one",
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
                text: "Cancel",
                type: ButtonType.secondary
            ),

            DialogButton(
                text: "Save",
                type: ButtonType.primary,
                onTap: () {
                  Navigator.of(context).pop(true);
                }
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
