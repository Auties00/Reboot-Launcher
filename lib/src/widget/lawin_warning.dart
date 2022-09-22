import 'package:fluent_ui/fluent_ui.dart';

class LawinWarning extends StatelessWidget {
  final VoidCallback onPressed;
  
  const LawinWarning({Key? key, required this.onPressed}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return InfoBar(
        title: const Text(
            "The lawin server handles authentication and parties, not game hosting"),
        action: IconButton(
            icon: const Icon(FluentIcons.accept),
            onPressed: onPressed
        )
    );
  }
}
