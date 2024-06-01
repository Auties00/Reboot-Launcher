import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/gestures.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/add_local_version.dart';
import 'package:reboot_launcher/src/widget/add_server_version.dart';
import 'package:reboot_launcher/src/widget/file_selector.dart';
import 'package:url_launcher/url_launcher.dart';

class VersionSelector extends StatefulWidget {
  const VersionSelector({Key? key}) : super(key: key);

  static Future<void> openDownloadDialog() => showAppDialog<bool>(
    builder: (context) => const AddServerVersion(),
  );

  static Future<void> openAddDialog() => showAppDialog<bool>(
    builder: (context) => const AddLocalVersion(),
  );

  @override
  State<VersionSelector> createState() => _VersionSelectorState();
}

class _VersionSelectorState extends State<VersionSelector> {
  final GameController _gameController = Get.find<GameController>();
  final RxBool _deleteFilesController = RxBool(false);
  final FlyoutController _flyoutController = FlyoutController();

  @override
  Widget build(BuildContext context) => Obx(() {
    return _createOptionsMenu(
      version: _gameController.selectedVersion,
      close: false,
      child: FlyoutTarget(
        controller: _flyoutController,
        child: DropDownButton(
            leading: Text(_gameController.selectedVersion?.name ?? translations.selectVersion),
            items: _createSelectorItems(context)
        ),
      )
  );
  });

  List<MenuFlyoutItem> _createSelectorItems(BuildContext context) {
    final items = _gameController.versions.value
      .map((version) => _createVersionItem(context, version))
      .toList();
    items.add(MenuFlyoutItem(
        text: Text(translations.addLocalBuildContent),
        onPressed: VersionSelector.openAddDialog
    ));
    items.add(MenuFlyoutItem(
        text: Text(translations.downloadBuildContent),
        onPressed: VersionSelector.openDownloadDialog
    ));
    return items;
  }

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

        var result = await _flyoutController.showFlyout<_ContextualOption?>(
            builder: (context) => MenuFlyout(
                items: _ContextualOption.values
                    .map((entry) => _createOption(context, entry))
                    .toList()
            )
        );
        _handleResult(result, version, close);
      },
      child: child
  );

  void _handleResult(_ContextualOption? result, FortniteVersion version, bool close) async {
    switch (result) {
      case _ContextualOption.openExplorer:
        if(!mounted){
          return;
        }

        if(close) {
          Navigator.of(context).pop();
        }

        launchUrl(version.location.uri)
            .onError((error, stackTrace) => _onExplorerError());
        break;
      case _ContextualOption.modify:
        if(!mounted){
          return;
        }

        if(close) {
          Navigator.of(context).pop();
        }

        await _openRenameDialog(context, version);
        break;
      case _ContextualOption.delete:
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

  MenuFlyoutItem _createOption(BuildContext context, _ContextualOption entry) {
    return MenuFlyoutItem(
        text: Text(entry.name),
        onPressed: () => Navigator.of(context).pop(entry)
    );
  }

  bool _onExplorerError() {
    showInfoBar(translations.missingVersion);
    return false;
  }

  Future<bool?> _openDeleteDialog(BuildContext context, FortniteVersion version) {
    return showAppDialog<bool>(
        builder: (context) => ContentDialog(
          content: Column(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.start,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              SizedBox(
                  width: double.infinity,
                  child: Text(translations.deleteVersionDialogTitle)
              ),

              const SizedBox(height: 12.0),

              Obx(() => Checkbox(
                  checked: _deleteFilesController.value,
                  onChanged: (bool? value) => _deleteFilesController.value = value ?? false,
                  content: Text(translations.deleteVersionFromDiskOption)
              ))
            ],
          ),
          actions: [
            Button(
              onPressed: () => Navigator.of(context).pop(false),
              child: Text(translations.deleteVersionCancel),
            ),
            Button(
              onPressed: ()  => Navigator.of(context).pop(true),
              child: Text(translations.deleteVersionConfirm),
            )
          ],
        )
    );
  }

  Future<String?> _openRenameDialog(BuildContext context, FortniteVersion version) {
    var nameController = TextEditingController(text: version.name);
    var pathController = TextEditingController(text: version.location.path);
    return showAppDialog<String?>(
        builder: (context) => FormDialog(
            content: Column(
              mainAxisSize: MainAxisSize.min,
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                InfoLabel(
                    label: translations.versionName,
                    child: TextFormBox(
                        controller: nameController,
                        placeholder: translations.newVersionNameLabel,
                        autofocus: true,
                        validator: (text) => checkChangeVersion(text)
                    )
                ),

                const SizedBox(
                    height: 16.0
                ),

                FileSelector(
                    placeholder: translations.newVersionNameLabel,
                    windowTitle: translations.gameFolderPlaceWindowTitle,
                    label: translations.gameFolderLabel,
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
                text: translations.newVersionNameConfirm,
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

enum _ContextualOption {
  openExplorer,
  modify,
  delete
}

extension _ContextualOptionExtension on _ContextualOption {
  String get name {
    return this == _ContextualOption.openExplorer ? translations.openInExplorer
        : this == _ContextualOption.modify ? translations.modify
        : translations.delete;
  }
}
