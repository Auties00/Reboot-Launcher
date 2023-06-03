import 'package:fluent_ui/fluent_ui.dart';

class SquaredPaneItem extends PaneItem {
  SquaredPaneItem({
    super.key,
    required super.title,
    required super.icon,
    required super.body,
  });

  @override
  Widget build(
      BuildContext context,
      bool selected,
      VoidCallback? onPressed, {
        PaneDisplayMode? displayMode,
        bool showTextOnTop = true,
        int? itemIndex,
        bool? autofocus,
      }) {
    return Column(
      children: [
        SizedBox.square(
            dimension: 48,
            child: icon
        ),
        title!
      ],
    );
  }
}
