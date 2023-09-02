import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:flutter/foundation.dart';
import 'package:reboot_launcher/src/dialog/message.dart';
import 'package:reboot_launcher/src/util/picker.dart';

class FileSelector extends StatefulWidget {
  final String placeholder;
  final String windowTitle;
  final bool allowNavigator;
  final TextEditingController controller;
  final String? Function(String?) validator;
  final AutovalidateMode? validatorMode;
  final String? extension;
  final String? label;
  final bool folder;

  const FileSelector(
      {required this.placeholder,
        required this.windowTitle,
        required this.controller,
        required this.validator,
        required this.folder,
        this.label,
        this.extension,
        this.validatorMode,
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
    return widget.label != null ? InfoLabel(
      label: widget.label!,
      child: _buildBody,
    ) : _buildBody;
  }

  Widget get _buildBody => TextFormBox(
      controller: widget.controller,
      placeholder: widget.placeholder,
      validator: widget.validator,
      autovalidateMode: widget.validatorMode ?? AutovalidateMode.onUserInteraction,
      suffix: !widget.allowNavigator ? null : Button(
          onPressed: _onPressed,
          child: const Icon(FluentIcons.open_folder_horizontal)
      )
  );

  void _onPressed() {
    if(_selecting){
      showMessage("Folder selector is already opened");
      return;
    }

    _selecting = true;
    if(widget.folder) {
      compute(openFolderPicker, widget.windowTitle)
          .then((value) => widget.controller.text = value ?? widget.controller.text)
          .then((_) => _selecting = false);
      return;
    }

    compute(openFilePicker, widget.extension!)
        .then((value) => widget.controller.text = value ?? widget.controller.text)
        .then((_) => _selecting = false);
  }
}