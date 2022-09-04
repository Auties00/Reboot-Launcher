import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter_desktop_folder_picker/flutter_desktop_folder_picker.dart';

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
                    validator: widget.validator)),
            if (widget.allowNavigator) const SizedBox(width: 8.0),
            if (widget.allowNavigator)
              IconButton(
                  icon: const Icon(FluentIcons.open_folder_horizontal),
                  onPressed: _onPressed)
          ],
        ));
  }

  void _onPressed() async {
    var result = await FlutterDesktopFolderPicker.openFolderPickerDialog(
        title: "Select the game folder");
    widget.controller.text = result ?? "";
  }
}
