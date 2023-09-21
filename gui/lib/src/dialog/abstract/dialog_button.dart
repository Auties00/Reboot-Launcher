import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class DialogButton extends StatefulWidget {
  final String? text;
  final Function()? onTap;
  final ButtonType type;

  const DialogButton(
      {Key? key,
        this.text,
        this.onTap,
        required this.type})
      : assert(type != ButtonType.primary || onTap != null,
        "OnTap handler cannot be null for primary buttons"),
        assert(type != ButtonType.primary || text != null,
        "Text cannot be null for primary buttons"),
        super(key: key);

  @override
  State<DialogButton> createState() => _DialogButtonState();
}

class _DialogButtonState extends State<DialogButton> {
  @override
  Widget build(BuildContext context) => widget.type == ButtonType.only ? _onlyButton : _button;

  SizedBox get _onlyButton => SizedBox(
      width: double.infinity,
      child: _button
  );

  Widget get _button => widget.type == ButtonType.primary ? _primaryButton : _secondaryButton;

  Widget get _primaryButton {
    return Button(
      onPressed: widget.onTap!,
      child: Text(widget.text!),
    );
  }

  Widget get _secondaryButton {
    return Button(
      onPressed: widget.onTap ?? _onDefaultSecondaryActionTap,
      child: Text(widget.text ?? translations.defaultDialogSecondaryAction),
    );
  }

  void _onDefaultSecondaryActionTap() => Navigator.of(context).pop(null);
}

enum ButtonType {
  primary,
  secondary,
  only
}
