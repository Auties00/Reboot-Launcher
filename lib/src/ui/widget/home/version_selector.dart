import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/gestures.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:reboot_launcher/src/ui/dialog/add_local_version.dart';
import 'package:reboot_launcher/src/ui/dialog/add_server_version.dart';
import 'package:reboot_launcher/src/ui/dialog/dialog.dart';
import 'package:reboot_launcher/src/ui/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/ui/widget/shared/smart_check_box.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:url_launcher/url_launcher.dart';

import 'package:reboot_launcher/src/ui/widget/shared/file_selector.dart';

class VersionSelector extends StatefulWidget {
  const VersionSelector({Key? key}) : super(key: key);

  static void openDownloadDialog(BuildContext context) async {
    await showDialog<bool>(
        context: context,
        builder: (dialogContext) => const AddServerVersion()
    );
  }

  static void openAddDialog(BuildContext context) async {
    await showDialog<bool>(
        context: context,
        builder: (context) => AddLocalVersion());
  }

  @override
  State<VersionSelector> createState() => _VersionSelectorState();
}

class _VersionSelectorState extends State<VersionSelector> {
  final GameController _gameController = Get.find<GameController>();
  final CheckboxController _deleteFilesController = CheckboxController();
  final FlyoutController _flyoutController = FlyoutController();

  @override
  Widget build(BuildContext context) => Obx(() => _createOptionsMenu(
        version: _gameController.selectedVersion,
        close: false,
        child: FlyoutTarget(
          controller: _flyoutController,
          child: DropDownButton(
              leading: Text(_gameController.selectedVersion?.name ?? "Select a version"),
              items: _createSelectorItems(context)
          ),
        )
    ));

  List<MenuFlyoutItem> _createSelectorItems(BuildContext context) => _gameController.hasNoVersions ? [_createDefaultVersionItem()]
      : _gameController.versions.value
      .map((version) => _createVersionItem(context, version))
      .toList();

  MenuFlyoutItem _createDefaultVersionItem() => MenuFlyoutItem(
      text: const Text("Please create or download a version"),
      onPressed: () {}
  );

  MenuFlyoutItem _createVersionItem(BuildContext context, FortniteVersion version) => MenuFlyoutItem(
      text: _createOptionsMenu(
        version: version,
        close: true,
        child: Text(version.name),
      ),
      onPressed: () => _gameController.selectedVersion = version
  );

  Widget _createOptionsMenu({required FortniteVersion? version, required bool close, required Widget child}) => Listener(
      onPointerDown: (event) async {
        if (event.kind != PointerDeviceKind.mouse || event.buttons != kSecondaryMouseButton) {
          return;
        }

        if(version == null) {
          return;
        }

        var result = await _flyoutController.showFlyout<ContextualOption?>(
            builder: (context) => MenuFlyout(
                items: ContextualOption.values
                    .map((entry) => _createOption(context, entry))
                    .toList()
            )
        );
        _handleResult(result, version, close);
      },
      child: child
  );

  void _handleResult(ContextualOption? result, FortniteVersion version, bool close) async {
    switch (result) {
      case ContextualOption.openExplorer:
        if(!mounted){
          return;
        }

        if(close) {
          Navigator.of(context).pop();
        }

        launchUrl(version.location.uri)
            .onError((error, stackTrace) => _onExplorerError());
        break;
      case ContextualOption.modify:
        if(!mounted){
          return;
        }

        if(close) {
          Navigator.of(context).pop();
        }

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

        if(close) {
          Navigator.of(context).pop();
        }

        _gameController.removeVersion(version);
        if (_deleteFilesController.value && await version.location.exists()) {
          delete(version.location);
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
                InfoLabel(
                  label: "Name",
                  child: TextFormBox(
                      controller: nameController,
                      placeholder: "Type the new version name",
                      autofocus: true,
                      validator: (text) => checkChangeVersion(text)
                  )
                ),

                const SizedBox(
                    height: 16.0
                ),

                FileSelector(
                    placeholder: "Type the new game folder",
                    windowTitle: "Select game folder",
                    label: "Path",
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
