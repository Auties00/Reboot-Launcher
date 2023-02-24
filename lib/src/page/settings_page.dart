
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/widget/shared/smart_switch.dart';
import 'package:url_launcher/url_launcher.dart';

import '../util/checks.dart';
import '../widget/setting/url_updater.dart';
import '../widget/shared/file_selector.dart';

class SettingsPage extends StatelessWidget {
  final SettingsController _settingsController = Get.find<SettingsController>();

  SettingsPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) =>
      _settingsController.advancedMode.value ? _advancedSettings : _easySettings;

  Widget get _advancedSettings => Column(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const RebootUpdaterInput(),
        _createFileSelector(),
        _createConsoleSelector(),
        _createGameSelector(),
        _createVersionInfo(),
        _createAdvancedSwitch()
      ]
  );

  Widget get _easySettings => SizedBox.expand(
    child: Column(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        const CircleAvatar(
            radius: 48,
            backgroundImage: AssetImage("assets/images/auties.png")),
        const SizedBox(
          height: 16.0,
        ),
        const Text("Made by Auties00"),
        const SizedBox(
          height: 4.0,
        ),
        _versionText,
        const SizedBox(
          height: 8.0,
        ),
        Button(
            child: const Text("Switch to advanced mode"),
            onPressed: () => _settingsController.advancedMode.value = true
        )
      ],
    ),
  );

  Widget _createAdvancedSwitch() => SmartSwitch(
      label: "Advanced Mode",
      value: _settingsController.advancedMode
  );

  Widget _createVersionInfo() => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      const Text("Version Status"),
      const SizedBox(height: 6.0),
      Button(
          child: _versionText,
          onPressed: () => launchUrl(safeBinariesDirectory.uri)
      )
    ],
  );

  Widget _createGameSelector() => Tooltip(
      message: "The dll that is injected to make the game work",
      child: FileSelector(
          label: "Cranium DLL",
          placeholder:
          "Type the path to the dll used for authentication",
          controller: _settingsController.authDll,
          windowTitle: "Select a dll",
          folder: false,
          extension: "dll",
          validator: checkDll,
          validatorMode: AutovalidateMode.always
      )
  );

  Widget _createConsoleSelector() => Tooltip(
    message: "The dll that is injected when a client is launched",
    child: FileSelector(
        label: "Console DLL",
        placeholder: "Type the path to the console dll",
        controller: _settingsController.consoleDll,
        windowTitle: "Select a dll",
        folder: false,
        extension: "dll",
        validator: checkDll,
        validatorMode: AutovalidateMode.always),
  );

  Widget _createFileSelector() => Tooltip(
    message: "The dll that is injected when a server is launched",
    child: FileSelector(
        label: "Reboot DLL",
        placeholder: "Type the path to the reboot dll",
        controller: _settingsController.rebootDll,
        windowTitle: "Select a dll",
        folder: false,
        extension: "dll",
        validator: checkDll,
        validatorMode: AutovalidateMode.always),
  );

  Widget get _versionText => const Text("6.4${kDebugMode ? '-DEBUG' : '-RELEASE'}");
}
