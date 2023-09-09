import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/widget/server/start_button.dart';
import 'package:reboot_launcher/src/widget/server/type_selector.dart';
import 'package:url_launcher/url_launcher.dart';

import 'package:reboot_launcher/src/widget/common/setting_tile.dart';

import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';

class AuthenticatorPage extends StatefulWidget {
  const AuthenticatorPage({Key? key}) : super(key: key);

  @override
  State<AuthenticatorPage> createState() => _AuthenticatorPageState();
}

class _AuthenticatorPageState extends State<AuthenticatorPage> with AutomaticKeepAliveClientMixin {
  final AuthenticatorController _authenticatorController = Get.find<AuthenticatorController>();

  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Obx(() => Column(
      children: [
        Expanded(
          child: ListView(
              children: [
                SettingTile(
                  title: "Authenticator configuration",
                  subtitle: "This section contains the authenticator's configuration",
                  content: const ServerTypeSelector(
                      authenticator: true
                  ),
                  expandedContent: [
                    if(_authenticatorController.type.value == ServerType.remote)
                      SettingTile(
                          title: "Host",
                          subtitle: "The hostname of the authenticator",
                          isChild: true,
                          content: TextFormBox(
                              placeholder: "Host",
                              controller: _authenticatorController.host
                          )
                      ),
                    if(_authenticatorController.type.value != ServerType.embedded)
                      SettingTile(
                          title: "Port",
                          subtitle: "The port of the authenticator",
                          isChild: true,
                          content: TextFormBox(
                              placeholder: "Port",
                              controller: _authenticatorController.port,
                              keyboardType: TextInputType.number,
                              inputFormatters: [
                                FilteringTextInputFormatter.digitsOnly
                              ]
                          )
                      ),
                    if(_authenticatorController.type.value == ServerType.embedded)
                      SettingTile(
                          title: "Detached",
                          subtitle: "Whether the embedded authenticator should be started as a separate process, useful for debugging",
                          contentWidth: null,
                          isChild: true,
                          content: Obx(() => ToggleSwitch(
                              checked: _authenticatorController.detached(),
                              onChanged: (value) => _authenticatorController.detached.value = value
                          ))
                      ),
                  ],
                ),
                const SizedBox(
                  height: 8.0,
                ),
                SettingTile(
                    title: "Installation directory",
                    subtitle: "Opens the folder where the embedded authenticator is located",
                    content: Button(
                        onPressed: () => launchUrl(authenticatorDirectory.uri),
                        child: const Text("Show Files")
                    )
                ),
                const SizedBox(
                  height: 8.0,
                ),
                SettingTile(
                    title: "Reset authenticator",
                    subtitle: "Resets the authenticator's settings to their default values",
                    content: Button(
                      onPressed: () => showAppDialog(
                          builder: (context) => InfoDialog(
                            text: "Do you want to reset all the setting in this tab to their default values? This action is irreversible",
                            buttons: [
                              DialogButton(
                                type: ButtonType.secondary,
                                text: "Close",
                              ),
                              DialogButton(
                                type: ButtonType.primary,
                                text: "Reset",
                                onTap: () {
                                  _authenticatorController.reset();
                                  Navigator.of(context).pop();
                                },
                              )
                            ],
                          )
                      ),
                      child: const Text("Reset"),
                    )
                )
              ]
          ),
        ),
        const SizedBox(
          height: 8.0,
        ),
        const ServerButton(
            authenticator: true
        )
      ],
    ));
  }

  bool get _isRemote => _authenticatorController.type.value == ServerType.remote;
}
