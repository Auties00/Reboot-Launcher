import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/widget/file_selector.dart';


class ScanLocalVersion extends StatefulWidget {
  const ScanLocalVersion({Key? key})
      : super(key: key);

  @override
  State<ScanLocalVersion> createState() => _ScanLocalVersionState();
}

class _ScanLocalVersionState extends State<ScanLocalVersion> {
  final TextEditingController _folderController = TextEditingController();
  Future<List<Directory>>? _future;

  @override
  Widget build(BuildContext context) {
    return Form(
        child: Builder(
            builder: (formContext) => ContentDialog(
                style: const ContentDialogThemeData(
                  padding: EdgeInsets.only(left: 20, right: 20, top: 20.0, bottom: 0.0)
                ),
                constraints: const BoxConstraints(maxWidth: 368, maxHeight: 169),
                content: _createLocalVersionDialogBody(),
                actions: _createLocalVersionActions(formContext))));
  }

  List<Widget> _createLocalVersionActions(BuildContext context) {
    if(_future == null) {
      return [
        Button(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Close'),
        ),
        FilledButton(
            child: const Text('Scan'),
            onPressed: () => _scanFolder(context, true))
      ];
    }

    return [
      FutureBuilder(
          future: _future,
          builder: (context, snapshot) {
            if(!snapshot.hasData || snapshot.hasError) {
              return SizedBox(
                width: double.infinity,
                child: Button(
                  onPressed: () => Navigator.of(context).pop(),
                  child: const Text('Close'),
                ),
              );
          }

            return SizedBox(
                width: double.infinity,
                child: FilledButton(
                    child: const Text('Save'),
                    onPressed: () => Navigator.of(context).pop()
                )
            );
          }
      )
    ];
  }

  Future<void> _scanFolder(BuildContext context, bool save) async {
    setState(() {
      _future = compute(scanInstallations, _folderController.text);
    });
  }

  Widget _createLocalVersionDialogBody() {
    if(_future == null) {
      return Column(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          FileSelector(
              label: "Location",
              placeholder: "Type the folder to scan",
              windowTitle: "Select the folder to scan",
              controller: _folderController,
              validator: _checkScanFolder,
              folder: true
          )
        ],
      );
    }
    return FutureBuilder<List<Directory>>(
        future: _future,
        builder: (context, snapshot) {
          if(snapshot.hasError) {
            return Padding(
              padding: const EdgeInsets.only(bottom: 16),
              child: SizedBox(
                  width: double.infinity,
                  child: Text(
                      "An error was occurred while scanning:${snapshot.error}",
                      textAlign: TextAlign.center)),
            );
          }

          if(!snapshot.hasData){
            return Padding(
              padding: const EdgeInsets.only(bottom: 16),
              child: InfoLabel(
                  label: "Searching...",
                  child: const SizedBox(width: double.infinity, child: ProgressBar())
              ),
            );
          }

          return Padding(
            padding: const EdgeInsets.only(bottom: 16),
            child: SizedBox(
                width: double.infinity,
                child: Column(
                  children: [
                    const Text(
                        "Successfully completed scan",
                        textAlign: TextAlign.center),

                    _createResultsDropDown(snapshot.data!)
                  ],
                )),
          );
        }
    );
  }

  Widget _createResultsDropDown(List<Directory> data) {
    return Expanded(
      child: DropDownButton(
          leading: const Text("Results"),
          items: data.map((element) => _createResultItem(element)).toList()
      ),
    );
  }

  MenuFlyoutItem _createResultItem(element) {
    return MenuFlyoutItem(
        text: SizedBox(
            width: double.infinity,
            child: Text(path.basename(element.path))
        ),
        trailing: const Expanded(child: SizedBox()),
        onPressed: () {});
  }

  String? _checkScanFolder(text) {
    if (text == null || text.isEmpty) {
      return 'Invalid folder to scan';
    }

    var directory = Directory(text);
    if (!directory.existsSync()) {
      return "Directory doesn't exist";
    }

    return null;
  }
}