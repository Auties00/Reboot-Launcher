import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';

class InfoTile extends StatelessWidget {
  final Key? expanderKey;
  final Text title;
  final Text content;

  const InfoTile({
    this.expanderKey,
    required this.title,
    required this.content
  });

  @override
  Widget build(BuildContext context) => Padding(
      padding: const EdgeInsets.only(
          bottom: 4.0
      ),
      child: Expander(
        key: expanderKey,
        header: Row(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            Icon(
                FluentIcons.info_24_regular
            ),
            const SizedBox(width: 16.0),
            title
          ],
        ),
        content: content,
      ),
    );
}
