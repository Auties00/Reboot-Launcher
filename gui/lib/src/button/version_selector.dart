import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:flutter/gestures.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/tile/setting_tile.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:reboot_launcher/src/messenger/info_bar.dart';
import 'package:reboot_launcher/src/messenger/overlay.dart';
import 'package:reboot_launcher/src/message/download_version.dart';
import 'package:reboot_launcher/src/message/import_version.dart';
import 'package:url_launcher/url_launcher.dart';

class VersionSelector extends StatefulWidget {
  const VersionSelector({Key? key}) : super(key: key);

  static SettingTile buildTile({
    required GlobalKey<OverlayTargetState> key
  }) => SettingTile(
      icon: Icon(
          FluentIcons.play_24_regular
      ),
      title: Text(translations.selectFortniteName),
      subtitle: Text(translations.selectFortniteDescription),
      contentWidth: null,
      content: ConstrainedBox(
          constraints: BoxConstraints(
            minWidth: SettingTile.kDefaultContentWidth,
          ),
          child: OverlayTarget(
            key: key,
            child: const VersionSelector(),
          )
      )
  );

  static Future<void> openImportDialog(GameVersion? version) => showRebootDialog<bool>(
      builder: (context) => ImportVersionDialog(
        version: version,
        closable: true,
      ),
      dismissWithEsc: true
  );

  static Future<void> openDownloadDialog() => showRebootDialog<bool>(
    builder: (context) => DownloadVersionDialog(
      closable: true,
    ),
    dismissWithEsc: true
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
        version: _gameController.selectedVersion.value,
        close: false,
        child: FlyoutTarget(
          controller: _flyoutController,
          child: DropDownButton(
              onOpen: () => inDialog = true,
            onClose: () => inDialog = false,
              leading: Text(
                _gameController.selectedVersion.value?.name ?? translations.selectVersion,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
              items: _createSelectorItems(context)
          ),
        )
    );
  });

  Widget _createOptionsMenu({required GameVersion? version, required bool close, required Widget child}) => Listener(
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
                    .map((entry) => _createOption(entry))
                    .toList()
            )
        );
        _handleResult(result, version, close);
      },
      child: child
  );

  List<MenuFlyoutItem> _createSelectorItems(BuildContext context) {
    final items = _gameController.versions.value
        .map((version) => _createVersionItem(version))
        .toList();
    items.add(MenuFlyoutItem(
        trailing: Padding(
          padding: const EdgeInsets.all(8.0),
          child: Icon(
              FluentIcons.add_24_regular,
              size: 12
          ),
        ),
        text: Text(translations.addVersion),
        onPressed: () =>  VersionSelector.openImportDialog(null)
    ));
    items.add(MenuFlyoutItem(
        trailing: Padding(
          padding: const EdgeInsets.all(8.0),
          child: Icon(
              FluentIcons.arrow_download_24_regular,
              size: 14
          ),
        ),
        text: Text(translations.downloadVersion),
        onPressed: VersionSelector.openDownloadDialog
    ));
    return items;
  }

  MenuFlyoutItem _createVersionItem(GameVersion version) => MenuFlyoutItem(
      text: Listener(
          onPointerDown: (event) async {
            if (event.kind != PointerDeviceKind.mouse || event.buttons != kSecondaryMouseButton) {
              return;
            }

            await _openVersionOptions(version);
          },
          child: Text(version.name)
      ),
      trailing: IconButton(
          onPressed: () => _openVersionOptions(version),
          icon: Icon(
            FluentIcons.more_vertical_24_regular
          )
      ),
      onPressed: () => _gameController.selectedVersion.value = version
  );

  Future<void> _openVersionOptions(GameVersion version) async {
    final result = await _flyoutController.showFlyout<_ContextualOption?>(
        builder: (context) => MenuFlyout(
            items: _ContextualOption.values
                .map((entry) => _createOption(entry))
                .toList()
        ),
        barrierDismissible: true,
        barrierColor: Colors.transparent
    );
    _handleResult(result, version, true);
  }

  void _handleResult(_ContextualOption? result, GameVersion version, bool close) async {
    if(!mounted){
      return;
    }

    switch (result) {
      case _ContextualOption.openExplorer:
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

        await VersionSelector.openImportDialog(version);
        break;
      case _ContextualOption.delete:
        final result = await _openDeleteDialog(version) ?? false;
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
      case null:
        break;
    }
  }

  MenuFlyoutItem _createOption(_ContextualOption entry) {
    return MenuFlyoutItem(
        text: Text(entry.translatedName),
        onPressed: () => Navigator.of(context).pop(entry)
    );
  }

  bool _onExplorerError() {
    showRebootInfoBar(
      translations.missingVersionError,
      severity: InfoBarSeverity.error,
      duration: infoBarLongDuration,
    );
    return false;
  }

  Future<bool?> _openDeleteDialog(GameVersion version) {
    return showRebootDialog<bool>(
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
            DialogButton(
              type: ButtonType.secondary,
              onTap: () => Navigator.of(context).pop(false),
              text: translations.deleteVersionCancel
            ),
            DialogButton(
              type: ButtonType.primary,
              onTap: ()  => Navigator.of(context).pop(true),
              text: translations.deleteVersionConfirm
            )
          ],
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
  String get translatedName {
    return this == _ContextualOption.openExplorer ? translations.openInExplorer
        : this == _ContextualOption.modify ? translations.modify
        : translations.delete;
  }
}
