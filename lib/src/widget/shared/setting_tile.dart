import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/widget/shared/fluent_card.dart';

class SettingTile extends StatefulWidget {
  static const double kDefaultContentWidth = 200.0;

  final String title;
  final String subtitle;
  final Widget? content;
  final double? contentWidth;
  final List<Widget>? expandedContent;

  const SettingTile(
      {Key? key,
        required this.title,
        required this.subtitle,
        this.content,
        this.contentWidth = kDefaultContentWidth,
        this.expandedContent})
      : super(key: key);

  @override
  State<SettingTile> createState() => _SettingTileState();
}

class _SettingTileState extends State<SettingTile> {
  @override
  Widget build(BuildContext context) {
    if(widget.expandedContent == null){
      return _contentCard;
    }

    return Mica(
      elevation: 1,
      child: Expander(
          initiallyExpanded: true,
          contentBackgroundColor: FluentTheme.of(context).menuColor,
          headerShape: (open) => const RoundedRectangleBorder(
            borderRadius: BorderRadius.vertical(top: Radius.circular(4.0)),
          ),
          header: ListTile(
              title: Text(widget.title),
              subtitle: Text(widget.subtitle)
          ),
          headerHeight: 72,
          trailing: SizedBox(
              width: widget.contentWidth,
              child: widget.content
          ),
          content: Column(
              children: widget.expandedContent!
          )
      ),
    );
  }

  Widget get _contentCard => FluentCard(
    child: ListTile(
      title: Text(widget.title),
      subtitle: Text(widget.subtitle),
      trailing: SizedBox(
          width: widget.contentWidth,
          child: widget.content
      ),
    ),
  );
}