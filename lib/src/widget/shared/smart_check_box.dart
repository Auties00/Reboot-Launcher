import 'package:fluent_ui/fluent_ui.dart';

class SmartCheckBox extends StatefulWidget {
  final CheckboxController controller;
  final Widget? content;
  const SmartCheckBox({Key? key, required this.controller, this.content}) : super(key: key);

  @override
  State<SmartCheckBox> createState() => _SmartCheckBoxState();
}

class _SmartCheckBoxState extends State<SmartCheckBox> {
  @override
  Widget build(BuildContext context) {
    return Checkbox(
        checked: widget.controller.value,
        onChanged: (checked) => setState(() => widget.controller.value = checked ?? false),
        content: widget.content
    );
  }
}

class CheckboxController {
  bool value;

  CheckboxController({this.value = false});
}
