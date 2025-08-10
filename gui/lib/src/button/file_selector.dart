import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:reboot_launcher/src/util/os.dart';

typedef FileSelectorValidator = String? Function(String?);

class FileSelector extends StatefulWidget {
  final String placeholder;
  final String windowTitle;
  final bool allowNavigator;
  final TextEditingController controller;
  final FileSelectorValidator? validator;
  final AutovalidateMode? validatorMode;
  final Key? validatorKey;
  final String? extension;
  final String? label;
  final bool folder;
  final void Function(String)? onSelected;

  const FileSelector(
      {required this.placeholder,
        required this.windowTitle,
        required this.controller,
        required this.folder,
        required this.allowNavigator,
        this.validator,
        this.validatorKey,
        this.label,
        this.extension,
        this.validatorMode,
        this.onSelected,
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
      key: widget.validatorKey,
      suffix: !widget.allowNavigator ? null : Button(
          onPressed: _onPressed,
          child: const Icon(FluentIcons.open_folder_horizontal)
      )
  );

  void _onPressed() {
    if(_selecting){
      return;
    }

    _selecting = true;
    if(widget.folder) {
      compute(openFolderPicker, widget.windowTitle)
          .then(_updateText)
          .then((_) => _selecting = false);
      return;
    }

    compute(openFilePicker, widget.extension!)
        .then(_updateText)
        .then((_) => _selecting = false);
  }

  void _updateText(String? value) {
    if(value != null) {
      widget.onSelected?.call(value);
    }

    var text = value ?? widget.controller.text;
    widget.controller.text = value ?? widget.controller.text;
    widget.controller.selection = TextSelection.collapsed(offset: text.length);
  }
}