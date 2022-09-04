import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart'
    show showMenu, PopupMenuEntry, PopupMenuItem;
import 'package:reboot_launcher/src/util/version_controller.dart';
import 'package:reboot_launcher/src/widget/add_local_version.dart';
import 'package:reboot_launcher/src/widget/add_server_version.dart';
import 'package:reboot_launcher/src/widget/smart_selector.dart';

import '../model/fortnite_version.dart';

class VersionSelector extends StatefulWidget {
  final VersionController controller;

  const VersionSelector({Key? key, required this.controller}) : super(key: key);

  @override
  State<VersionSelector> createState() => _VersionSelectorState();
}

class _VersionSelectorState extends State<VersionSelector> {
  final StreamController _streamController = StreamController();

  @override
  Widget build(BuildContext context) {
    return InfoLabel(
        label: "Version",
        child: Align(
            alignment: AlignmentDirectional.centerStart,
            child: Row(
              children: [
                Expanded(
                    child: StreamBuilder(
                        stream: _streamController.stream,
                        builder: (context, snapshot) => SmartSelector(
                            keyName: "version",
                            placeholder: "Select a version",
                            options: widget.controller.isEmpty ? ["No versions available"] : widget.controller.versions
                                .map((element) => element.name)
                                .toList(),
                            useFirstItemByDefault: false,
                            itemBuilder: (name) => _createVersionItem(name, widget.controller.versions.isNotEmpty),
                            onSelected: _onSelected,
                            serializer: false,
                            initialValue: widget.controller.selectedVersion?.name,
                            enabled: widget.controller.versions.isNotEmpty
                        )
                    )
                ),
                const SizedBox(
                  width: 16,
                ),
                Tooltip(
                  message: "Add a local fortnite build to the versions list",
                  child: Button(
                      child: const Icon(FluentIcons.open_file),
                      onPressed: () => _openLocalVersionDialog(context)
                  ),
                ),
                const SizedBox(
                  width: 16,
                ),
                Tooltip(
                  message: "Download a fortnite build from the archive",
                  child: Button(
                      child: const Icon(FluentIcons.download),
                      onPressed: () => _openDownloadVersionDialog(context)),
                )
              ],
            )));
  }

  void _onSelected(String selected) {
    widget.controller.selectedVersion = widget.controller.versions
        .firstWhere((element) => selected == element.name);
  }

  SmartSelectorItem _createVersionItem(String name, bool enabled) {
    return SmartSelectorItem(
        text: _withListener(name, enabled, SizedBox(width: double.infinity, child: Text(name))),
        trailing: const Expanded(child: SizedBox()));
  }

  Listener _withListener(String name, bool enabled, Widget child) {
    return Listener(
          onPointerDown: (event) {
            if (event.kind != PointerDeviceKind.mouse ||
                event.buttons != kSecondaryMouseButton
                || !enabled) {
              return;
            }

            _openMenu(context, name, event.position);
          },
          child: child
      );
  }

  void _openDownloadVersionDialog(BuildContext context) async {
   await showDialog<bool>(
        context: context,
        builder: (dialogContext) => AddServerVersion(
            controller: widget.controller,
            onCancel: ()  => WidgetsBinding.instance
                .addPostFrameCallback((_) => showSnackbar(
                context,
                const Snackbar(content: Text("Download cancelled"))
            ))
        )
    );

    _streamController.add(true);
  }

  void _openLocalVersionDialog(BuildContext context) async {
    var result = await showDialog<bool>(
        context: context,
        builder: (context) => AddLocalVersion(controller: widget.controller));

    if(result == null || !result){
      return;
    }

    _streamController.add(false);
  }

  void _openMenu(
      BuildContext context, String name, Offset offset) {
    showMenu(
      context: context,
      items: <PopupMenuEntry>[
        const PopupMenuItem(value: 0, child: Text("Open in explorer")),
        const PopupMenuItem(value: 1, child: Text("Delete"))
      ],
      position: RelativeRect.fromLTRB(offset.dx, offset.dy, offset.dx, offset.dy),
    ).then((value) {
      if(value == 0){
        Navigator.of(context).pop();
        Process.run(
            "explorer.exe",
            [widget.controller.versions.firstWhere((element) => element.name == name).location.path]
        );
        return;
      }

      if(value != 1) {
        return;
      }

      Navigator.of(context).pop();
      var version = widget.controller.removeByName(name);
      _openDeleteDialog(context, version);
      _streamController.add(false);
      if (widget.controller.selectedVersion?.name != name &&
          widget.controller.isNotEmpty) {
        return;
      }

      widget.controller.selectedVersion = null;
      _streamController.add(false);
    });
  }

  void _openDeleteDialog(BuildContext context, FortniteVersion version) {
    showDialog(
        context: context,
        builder: (context) => ContentDialog(
              content: const SizedBox(
                  height: 32,
                  width: double.infinity,
                  child: Text("Delete associated game path?",
                      textAlign: TextAlign.center)),
              actions: [
                FilledButton(
                  onPressed: () => Navigator.of(context).pop(),
                  style: ButtonStyle(
                      backgroundColor: ButtonState.all(Colors.green)),
                  child: const Text('Keep'),
                ),
                FilledButton(
                  onPressed: () {
                    Navigator.of(context).pop();
                    version.location.delete();
                  },
                  style:
                      ButtonStyle(backgroundColor: ButtonState.all(Colors.red)),
                  child: const Text('Delete'),
                )
              ],
            ));
  }
}
