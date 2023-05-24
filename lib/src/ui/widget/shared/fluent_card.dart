import 'package:fluent_ui/fluent_ui.dart';

class FluentCard extends StatelessWidget {
  final Widget child;
  const FluentCard({Key? key, required this.child}) : super(key: key);

  @override
  Widget build(BuildContext context) => Mica(
      elevation: 1,
      child: Card(
          backgroundColor: FluentTheme.of(context).menuColor,
          borderRadius: const BorderRadius.vertical(top: Radius.circular(4.0)),
          child: child
      )
  );
}
