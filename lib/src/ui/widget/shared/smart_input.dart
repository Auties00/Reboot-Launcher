import 'package:fluent_ui/fluent_ui.dart';

class SmartInput extends StatelessWidget {
  final String? label;
  final String placeholder;
  final TextEditingController controller;
  final TextInputType type;
  final bool enabled;
  final VoidCallback? onTap;
  final bool readOnly;
  final AutovalidateMode validatorMode;
  final String? Function(String?)? validator;

  const SmartInput(
      {Key? key,
        required this.placeholder,
        required this.controller,
        this.label,
        this.onTap,
        this.enabled = true,
        this.readOnly = false,
        this.type = TextInputType.text,
        this.validatorMode = AutovalidateMode.disabled,
        this.validator})
      : super(key: key);
  
  @override
  Widget build(BuildContext context) {
    if(label != null){
      return InfoLabel(
        label: label!,
        child: _body
      );
    }

    return _body;
  }

  TextFormBox get _body => TextFormBox(
      enabled: enabled,
      controller: controller,
      keyboardType: type,
      placeholder: placeholder,
      onTap: onTap,
      readOnly: readOnly,
      autovalidateMode: validatorMode,
      validator: validator
  );
}
