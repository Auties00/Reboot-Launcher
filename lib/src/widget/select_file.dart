import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';

import '../util/os.dart';

class SelectFile extends StatefulWidget {
  final String label;
  final String placeholder;
  final String windowTitle;
  final bool allowNavigator;
  final TextEditingController controller;
  final String? Function(String?) validator;

  const SelectFile(
      {required this.label,
      required this.placeholder,
      required this.windowTitle,
      required this.controller,
      required this.validator,
      this.allowNavigator = true,
      Key? key})
      : super(key: key);

  @override
  State<SelectFile> createState() => _SelectFileState();
}

class _SelectFileState extends State<SelectFile> {
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
                  message: "Select a folder",
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
    compute(openFilePicker, "Select the game folder")
        .then((value) => widget.controller.text = value ?? "")
        .then((_) => _selecting = false);
  }
}