import 'package:fluent_ui/fluent_ui.dart';

class SmartInput extends StatelessWidget {
  final String? label;
  final String placeholder;
  final TextEditingController controller;
  final TextInputType type;
  final bool enabled;
  final VoidCallback? onTap;
  final bool readOnly;

  const SmartInput(
      {Key? key,
        required this.placeholder,
        required this.controller,
        this.label,
        this.onTap,
        this.enabled = true,
        this.readOnly = false,
        this.type = TextInputType.text})
      : super(key: key);
  
  @override
  Widget build(BuildContext context) {
    return TextBox(
      enabled: enabled,
      controller: controller,
      header: label,
      keyboardType: type,
      placeholder: placeholder,
      onTap: onTap,
      readOnly: readOnly,
    );
  }
}
