import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/gestures.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/dialog/add_local_version.dart';
import 'package:reboot_launcher/src/widget/shared/smart_check_box.dart';
import 'package:url_launcher/url_launcher.dart';

import 'package:reboot_launcher/src/dialog/add_server_version.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import '../shared/file_selector.dart';

class VersionSelector extends StatefulWidget {
  const VersionSelector({Key? key}) : super(key: key);

  @override
  State<VersionSelector> createState() => _VersionSelectorState();
}

class _VersionSelectorState extends State<VersionSelector> {
  final GameController _gameController = Get.find<GameController>();
  final CheckboxController _deleteFilesController = CheckboxController();

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
      child: Obx(() => DropDownButton(
              leading: Text(_gameController.selectedVersionObs.value?.name ??
                  "Select a version"),
              items: _gameController.hasNoVersions
                  ? [_createDefaultVersionItem()]
                  : _gameController.versions.value
                  .map((version) => _createVersionItem(context, version))
                  .toList()))
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
          child: SizedBox(
              width: double.infinity,
              child: Text(version.name)
          ),
        ),
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

  Future<void> _openMenu(
      BuildContext context, FortniteVersion version, Offset offset) async {
    var result = await showMenu<ContextualOption>(
        context: context,
        offset: offset,
        builder: (context) => MenuFlyout(
            items: ContextualOption.values
                .map((entry) => _createOption(context, entry))
                .toList()
        )
    );

    switch (result) {
      case ContextualOption.openExplorer:
        if(!mounted){
          return;
        }

        Navigator.of(context).pop();
        launchUrl(version.location.uri)
            .onError((error, stackTrace) => _onExplorerError());
        break;

      case ContextualOption.modify:
        if(!mounted){
          return;
        }

        Navigator.of(context).pop();
        await _openRenameDialog(context, version);
        break;

      case ContextualOption.delete:
        if(!mounted){
          return;
        }

        var result = await _openDeleteDialog(context, version) ?? false;
        if(!mounted || !result){
          return;
        }

        Navigator.of(context).pop();

        _gameController.removeVersion(version);
        if (_gameController.selectedVersionObs.value?.name == version.name || _gameController.hasNoVersions) {
          _gameController.selectedVersionObs.value = null;
        }

        if (_deleteFilesController.value && await version.location.exists()) {
          version.location.delete(recursive: true);
        }

        break;

      default:
        break;
    }
  }

  MenuFlyoutItem _createOption(BuildContext context, ContextualOption entry) {
    return MenuFlyoutItem(
        text: Text(entry.name),
        onPressed: () => Navigator.of(context).pop(entry)
    );
  }

  bool _onExplorerError() {
    showSnackbar(
        context,
        const Snackbar(
            content: Text("This version doesn't exist on the local machine", textAlign: TextAlign.center),
            extended: true
        )
    );
    return false;
  }

  Future<bool?> _openDeleteDialog(BuildContext context, FortniteVersion version) {
    return showDialog<bool>(
        context: context,
        builder: (context) => ContentDialog(
              content: Column(
                mainAxisSize: MainAxisSize.min,
                mainAxisAlignment: MainAxisAlignment.start,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const SizedBox(
                      width: double.infinity,
                      child: Text("Are you sure you want to delete this version?")),

                  const SizedBox(height: 12.0),

                  SmartCheckBox(
                      controller: _deleteFilesController,
                      content: const Text("Delete version files from disk")
                  )
                ],
              ),
              actions: [
                Button(
                  onPressed: () => Navigator.of(context).pop(false),
                  child: const Text('Keep'),
                ),
                FilledButton(
                  onPressed: ()  => Navigator.of(context).pop(true),
                  child: const Text('Delete'),
                )
              ],
            )
    );
  }

  Future<String?> _openRenameDialog(BuildContext context, FortniteVersion version) {
    var nameController = TextEditingController(text: version.name);
    var pathController = TextEditingController(text: version.location.path);
    return showDialog<String?>(
        context: context,
        builder: (context) => FormDialog(
            content: Column(
              mainAxisSize: MainAxisSize.min,
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                TextFormBox(
                    controller: nameController,
                    header: "Name",
                    placeholder: "Type the new version name",
                    autofocus: true,
                    validator: (text) => checkVersion(text, _gameController.versions.value)
                ),

                const SizedBox(
                    height: 16.0
                ),

                FileSelector(
                    label: "Location",
                    placeholder: "Type the new game folder",
                    windowTitle: "Select game folder",
                    controller: pathController,
                    validator: checkGameFolder,
                    folder: true
                ),

                const SizedBox(height: 8.0),
              ],
            ),
            buttons: [
              DialogButton(
                  type: ButtonType.secondary
              ),

              DialogButton(
                text: "Save",
                type: ButtonType.primary,
                onTap: () {
                  Navigator.of(context).pop();
                  _gameController.updateVersion(version, (version) {
                    version.name = nameController.text;
                    version.location = Directory(pathController.text);
                  });
                },
              )
            ]
        )
    );
  }
}

enum ContextualOption {
  openExplorer,
  modify,
  delete;

  String get name {
    return this == ContextualOption.openExplorer ? "Open in explorer"
        : this == ContextualOption.modify ? "Modify"
        : "Delete";
  }
}
