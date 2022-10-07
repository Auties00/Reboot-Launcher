import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';

import '../util/os.dart';

class FileSelector extends StatefulWidget {
  final String label;
  final String placeholder;
  final String windowTitle;
  final bool allowNavigator;
  final TextEditingController controller;
  final String? Function(String?) validator;
  final String? extension;
  final bool folder;

  const FileSelector(
      {required this.label,
      required this.placeholder,
      required this.windowTitle,
      required this.controller,
      required this.validator,
      required this.folder,
      this.extension,
      this.allowNavigator = true,
      Key? key})
      : assert(folder || extension != null, "Missing extension for file selector"),
        super(key: key);

  @override
  State<FileSelector> createState() => _FileSelectorState();
}

class _FileSelectorState extends State<FileSelector> {
  bool _selecting = false;

  @override
  Widget build(BuildContext context) {
    return InfoLabel(
        label: widget.label,
        child: Row(
          children: [
            Expanded(
                child: TextFormBox(
                    controller: widget.controller,
                    placeholder: widget.placeholder,
                    validator: widget.validator,
                    hidePadding: true
                )
            ),
            if (widget.allowNavigator) const SizedBox(width: 8.0),
            if (widget.allowNavigator)
              Padding(
                padding: const EdgeInsets.only(bottom: 21.0),
                child: Tooltip(
                  message: "Select a ${widget.folder ? 'folder' : 'file'}",
                  child: Button(
                      onPressed: _onPressed,
                      child: const Icon(FluentIcons.open_folder_horizontal)
                  ),
                ),
              )
          ],
        )
    );
  }

  void _onPressed() {
    if(_selecting){
      showSnackbar(context, const Snackbar(content: Text("Folder selector is already opened")));
      return;
    }

    _selecting = true;
    if(widget.folder) {
      compute(openFolderPicker, widget.windowTitle)
          .then((value) => widget.controller.text = value ?? "")
          .then((_) => _selecting = false);
      return;
    }

    compute(openFilePicker, widget.extension!)
        .then((value) => widget.controller.text = value ?? "")
        .then((_) => _selecting = false);
  }
}