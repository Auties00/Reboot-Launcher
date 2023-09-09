import 'package:clipboard/clipboard.dart';
import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/main.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/update_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:flutter/material.dart' show Icons;

import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/widget/game/start_button.dart';
import 'package:reboot_launcher/src/widget/version/version_selector.dart';

class HostingPage extends StatefulWidget {
  const HostingPage({Key? key}) : super(key: key);

  @override
  State<HostingPage> createState() => _HostingPageState();
}

class _HostingPageState extends State<HostingPage> with AutomaticKeepAliveClientMixin {
  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final UpdateController _updateController = Get.find<UpdateController>();
  late final RxBool _showPasswordTrailing = RxBool(_hostingController.password.text.isNotEmpty);

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
              SettingTile(
                title: "Game Server",
                subtitle: "Provide basic information about your server",
                expandedContent: [
                  SettingTile(
                      title: "Name",
                      subtitle: "The name of your game server",
                      isChild: true,
                      content: TextFormBox(
                          placeholder: "Name",
                          controller: _hostingController.name
                      )
                  ),
                  SettingTile(
                      title: "Description",
                      subtitle: "The description of your game server",
                      isChild: true,
                      content: TextFormBox(
                          placeholder: "Description",
                          controller: _hostingController.description
                      )
                  ),
                  SettingTile(
                      title: "Password",
                      subtitle: "The password of your game server for the server browser",
                      isChild: true,
                      content: Obx(() => TextFormBox(
                          placeholder: "Password",
                          controller: _hostingController.password,
                          autovalidateMode: AutovalidateMode.always,
                          obscureText: !_hostingController.showPassword.value,
                          enableSuggestions: false,
                          autocorrect: false,
                          onChanged: (text) => _showPasswordTrailing.value = text.isNotEmpty,
                          suffix: Button(
                            onPressed: () => _hostingController.showPassword.value = !_hostingController.showPassword.value,
                            style: ButtonStyle(
                                shape: ButtonState.all(const CircleBorder()),
                                backgroundColor: ButtonState.all(Colors.transparent)
                            ),
                            child: Icon(
                                _hostingController.showPassword.value ? Icons.visibility_off : Icons.visibility,
                                color: _showPasswordTrailing.value ? null : Colors.transparent
                            ),
                          )
                      ))
                  ),
                  SettingTile(
                      title: "Discoverable",
                      subtitle: "Make your server available to other players on the server browser",
                      isChild: true,
                      contentWidth: null,
                      content: Obx(() => ToggleSwitch(
                          checked: _hostingController.discoverable(),
                          onChanged: (value) => _hostingController.discoverable.value = value
                      ))
                  )
                ],
              ),
              const SizedBox(
                height: 8.0,
              ),
              const SettingTile(
                  title: "Version",
                  subtitle: "Select the version of Fortnite you want to host",
                  content: VersionSelector(),
                  expandedContent: [
                    SettingTile(
                        title: "Add a version from this PC's local storage",
                        subtitle: "Versions coming from your local disk are not guaranteed to work",
                        content: Button(
                          onPressed: VersionSelector.openAddDialog,
                          child: Text("Add build"),
                        ),
                        isChild: true
                    ),
                    SettingTile(
                        title: "Download any version from the cloud",
                        subtitle: "Download any Fortnite build easily from the cloud",
                        content: Button(
                          onPressed: VersionSelector.openDownloadDialog,
                          child: Text("Download"),
                        ),
                        isChild: true
                    )
                  ]
              ),
              const SizedBox(
                height: 8.0
              ),
              SettingTile(
                  title: "Share",
                  subtitle: "Make it easy for other people to join your server with the options in this section",
                  expandedContent: [
                    SettingTile(
                      title: "Link",
                      subtitle: "Copies a link for your server to the clipboard (requires the Reboot Launcher)",
                      isChild: true,
                      content: Button(
                        onPressed: () async {
                          FlutterClipboard.controlC("$kCustomUrlSchema://${_gameController.uuid}");
                          showInfoBar(
                              "Copied your link to the clipboard",
                              severity: InfoBarSeverity.success
                          );
                        },
                        child: const Text("Copy Link"),
                      )
                    ),
                    SettingTile(
                        title: "Public IP",
                        subtitle: "Copies your current public IP to the clipboard (doesn't require the Reboot Launcher)",
                        isChild: true,
                        content: Button(
                          onPressed: () async {
                            try {
                              showInfoBar(
                                  "Obtaining your public IP...",
                                  loading: true,
                                  duration: null
                              );
                              var ip = await Ipify.ipv4();
                              FlutterClipboard.controlC(ip);
                              showInfoBar(
                                  "Copied your IP to the clipboard",
                                  severity: InfoBarSeverity.success
                              );
                            }catch(error) {
                              showInfoBar(
                                  "An error occurred while obtaining your public IP: $error",
                                  severity: InfoBarSeverity.error,
                                duration: snackbarLongDuration
                              );
                            }
                          },
                          child: const Text("Copy IP"),
                        )
                    )
                  ],
              )
            ],
          ),
        ),
        const SizedBox(
          height: 8.0,
        ),
        const LaunchButton(
            host: true
        )
      ],
    );
  }
}