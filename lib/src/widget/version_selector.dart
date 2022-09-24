// ignore_for_file: use_build_context_synchronously

import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart'
    show showMenu, PopupMenuEntry, PopupMenuItem;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/widget/add_local_version.dart';
import 'package:reboot_launcher/src/widget/add_server_version.dart';

import 'package:reboot_launcher/src/model/fortnite_version.dart';

import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/widget/scan_local_version.dart';

import '../controller/build_controller.dart';

class VersionSelector extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();
  final bool enableScanner;

  VersionSelector({Key? key, this.enableScanner = false}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return InfoLabel(
        label: "Version",
        child: Align(
            alignment: AlignmentDirectional.centerStart,
            child: Row(
              children: [
                Expanded(child: _createSelector(context)),
                const SizedBox(
                  width: 16,
                ),
                Tooltip(
                  message: "Add a local fortnite build to the versions list",
                  child: Button(
                      child: const Icon(FluentIcons.open_file),
                      onPressed: () => _openAddLocalVersionDialog(context)),
                ),
                const SizedBox(
                  width: 16,
                ),
                if(enableScanner)
                  Tooltip(
                    message: "Scan all fortnite builds in a directory",
                    child: Button(
                        child: const Icon(FluentIcons.site_scan),
                        onPressed: () => _openScanLocalVersionDialog(context)),
                  ),
                if(enableScanner)
                  const SizedBox(
                    width: 16,
                  ),
                Tooltip(
                  message: "Download a fortnite build from the archive",
                  child: Button(
                      child: const Icon(FluentIcons.download),
                      onPressed: () => _openDownloadVersionDialog(context)),
                ),
              ],
            )));
  }

  Widget _createSelector(BuildContext context) {
    return Tooltip(
      message: "The version of Fortnite to launch",
      child: SizedBox(
        width: double.infinity,
        child: Obx(() => DropDownButton(
              leading: Text(_gameController.selectedVersionObs.value?.name ??
                  "Select a version"),
              items: _gameController.hasNoVersions
                  ? [_createDefaultVersionItem()]
                  : _gameController.versions.value
                  .map((version) => _createVersionItem(context, version))
                  .toList()))
      ),
    );
  }

  MenuFlyoutItem _createVersionItem(
      BuildContext context, FortniteVersion version) {
    return MenuFlyoutItem(
        text: Listener(
            onPointerDown: (event) async {
              if (event.kind != PointerDeviceKind.mouse ||
                  event.buttons != kSecondaryMouseButton) {
                return;
              }

              await _openMenu(context, version, event.position);
            },
            child: SizedBox(width: double.infinity, child: Text(version.name))),
        trailing: const Expanded(child: SizedBox()),
        onPressed: () => _gameController.selectedVersion = version);
  }

  MenuFlyoutItem _createDefaultVersionItem() {
    return MenuFlyoutItem(
        text: const SizedBox(
            width: double.infinity, child: Text("No versions available")),
        trailing: const Expanded(child: SizedBox()),
        onPressed: () {});
  }

  void _openDownloadVersionDialog(BuildContext context) async {
    await showDialog<bool>(
        context: context,
        builder: (dialogContext) => const AddServerVersion()
    );
  }

  void _openAddLocalVersionDialog(BuildContext context) async {
    await showDialog<bool>(
        context: context,
        builder: (context) => AddLocalVersion());
  }

  void _openScanLocalVersionDialog(BuildContext context) async {
    await showDialog<bool>(
        context: context,
        builder: (context) => ScanLocalVersion());
  }

  Future<void> _openMenu(
      BuildContext context, FortniteVersion version, Offset offset) async {
    var result = await showMenu(
      context: context,
      items: <PopupMenuEntry>[
        const PopupMenuItem(value: 0, child: Text("Open in explorer")),
        const PopupMenuItem(value: 1, child: Text("Delete"))
      ],
      position:
          RelativeRect.fromLTRB(offset.dx, offset.dy, offset.dx, offset.dy),
    );

    switch (result) {
      case 0:
        Navigator.of(context).pop();
        Process.run("explorer.exe", [version.location.path]);
        break;

      case 1:
        _gameController.removeVersion(version);
        await _openDeleteDialog(context, version);
        Navigator.of(context).pop();
        if (_gameController.selectedVersionObs.value?.name == version.name || _gameController.hasNoVersions) {
          _gameController.selectedVersionObs.value = null;
        }

        break;
    }
  }

  Future _openDeleteDialog(BuildContext context, FortniteVersion version) {
    return showDialog(
        context: context,
        builder: (context) => ContentDialog(
              content: const SizedBox(
                  width: double.infinity,
                  child: Text("Do you want to also delete the files for this version?",
                      textAlign: TextAlign.center)),
              actions: [
                FilledButton(
                  onPressed: () => Navigator.of(context).pop(),
                  child: const Text('Keep'),
                ),
                FilledButton(
                  onPressed: () async {
                    Navigator.of(context).pop();
                    if (await version.location.exists()) {
                      version.location.delete(recursive: true);
                    }
                  },
                  style:
                      ButtonStyle(backgroundColor: ButtonState.all(Colors.red)),
                  child: const Text('Delete'),
                )
              ],
            ));
  }
}
