import 'package:fluent_ui/fluent_ui.dart';

class RestartWarning extends StatelessWidget {
  const RestartWarning({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return const InfoBar(
        title: Text('Node Installation'),
        content: Text('Restart the launcher to run the server'),
        isLong: true,
        severity: InfoBarSeverity.warning
    );
  }
}
