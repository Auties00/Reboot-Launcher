import 'package:fluent_ui/fluent_ui.dart';

class WarningInfo extends StatelessWidget {
  final String text;
  final VoidCallback onPressed;
  final IconData icon;
  final InfoBarSeverity severity;

  const WarningInfo(
      {Key? key,
        required this.text,
        required this.icon,
        required this.onPressed,
        this.severity = InfoBarSeverity.info})
      : super(key: key);

  @override
  Widget build(BuildContext context) {
    return InfoBar(
        severity: severity,
        title: Text(text),
        action: IconButton(
            icon: Icon(icon),
            onPressed: onPressed
        )
    );
  }
}
