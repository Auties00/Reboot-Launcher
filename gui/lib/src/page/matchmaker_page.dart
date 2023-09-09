import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:reboot_launcher/src/widget/server/start_button.dart';
import 'package:reboot_launcher/src/widget/server/type_selector.dart';
import 'package:url_launcher/url_launcher.dart';

class MatchmakerPage extends StatefulWidget {
  const MatchmakerPage({Key? key}) : super(key: key);

  @override
  State<MatchmakerPage> createState() => _MatchmakerPageState();
}

class _MatchmakerPageState extends State<MatchmakerPage> with AutomaticKeepAliveClientMixin {
  final MatchmakerController _matchmakerController = Get.find<MatchmakerController>();

  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Column(
      children: [
        Expanded(
          child: ListView(
              children: [
                Obx(() => SettingTile(
                    title: "Matchmaker configuration",
                    subtitle: "This section contains the matchmaker's configuration",
                    content: const ServerTypeSelector(
                        authenticator: false
                    ),
                    expandedContent: [
                      if(_matchmakerController.type.value == ServerType.remote)
                        SettingTile(
                            title: "Host",
                            subtitle: "The hostname of the matchmaker",
                            isChild: true,
                            content: TextFormBox(
                                placeholder: "Host",
                                controller: _matchmakerController.host
                            )
                        ),
                      if(_matchmakerController.type.value != ServerType.embedded)
                        SettingTile(
                            title: "Port",
                            subtitle: "The port of the matchmaker",
                            isChild: true,
                            content: TextFormBox(
                                placeholder: "Port",
                                controller: _matchmakerController.port,
                                keyboardType: TextInputType.number,
                                inputFormatters: [
                                  FilteringTextInputFormatter.digitsOnly
                                ]
                            )
                        ),
                      if(_matchmakerController.type.value == ServerType.embedded)
                        SettingTile(
                            title: "Game server address",
                            subtitle: "The address of the game server used by the matchmaker",
                            isChild: true,
                            content: TextFormBox(
                                placeholder: "Address",
                                controller: _matchmakerController.gameServerAddress,
                                focusNode: _matchmakerController.gameServerAddressFocusNode
                            )
                        ),
                      if(_matchmakerController.type.value == ServerType.embedded)
                        SettingTile(
                          title: "Detached",
                          subtitle: "Whether the embedded matchmaker should be started as a separate process, useful for debugging",
                          contentWidth: null,
                          isChild: true,
                          content: Obx(() => Row(
                            children: [
                              Text(
                                  _matchmakerController.detached.value ? "On" : "Off"
                              ),
                              const SizedBox(
                                width: 16.0
                              ),
                              ToggleSwitch(
                                  checked: _matchmakerController.detached.value,
                                  onChanged: (value) => _matchmakerController.detached.value = value
                              ),
                            ],
                          )),
                        )
                    ]
                )),
                const SizedBox(
                  height: 8.0,
                ),
                SettingTile(
                    title: "Installation directory",
                    subtitle: "Opens the folder where the embedded matchmaker is located",
                    content: Button(
                        onPressed: () => launchUrl(matchmakerDirectory.uri),
                        child: const Text("Show Files")
                    )
                ),
                const SizedBox(
                  height: 8.0,
                ),
                SettingTile(
                    title: "Reset matchmaker",
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
                                  _matchmakerController.reset();
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
          authenticator: false
        )
      ],
    );
  }
}
