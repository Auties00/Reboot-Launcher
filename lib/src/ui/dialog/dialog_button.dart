import 'package:fluent_ui/fluent_ui.dart';

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
  Widget build(BuildContext context) {
    return widget.type == ButtonType.only ? _createOnlyButton() : _createButton();
  }

  SizedBox _createOnlyButton() {
    return SizedBox(
        width: double.infinity,
        child: _createButton()
    );
  }

  Widget _createButton() {
    return widget.type == ButtonType.primary ? _createPrimaryActionButton()
      : _createSecondaryActionButton();
  }

  Widget _createPrimaryActionButton() {
    return FilledButton(
      onPressed: widget.onTap!,
      child: Text(widget.text!),
    );
  }

  Widget _createSecondaryActionButton() {
    return Button(
      onPressed: widget.onTap ?? _onDefaultSecondaryActionTap,
      child: Text(widget.text ?? "Close"),
    );
  }

  void _onDefaultSecondaryActionTap() {
    Navigator.of(context).pop(null);
  }
}

enum ButtonType {
  primary,
  secondary,
  only
}
