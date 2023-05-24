import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/ui/widget/shared/fluent_card.dart';

class SettingTile extends StatefulWidget {
  static const double kDefaultContentWidth = 200.0;
  static const double kDefaultSpacing = 8.0;

  final String title;
  final String subtitle;
  final Widget? content;
  final double? contentWidth;
  final List<Widget>? expandedContent;
  final double expandedContentSpacing;
  final bool isChild;

  const SettingTile(
      {Key? key,
        required this.title,
        required this.subtitle,
        this.content,
        this.contentWidth = kDefaultContentWidth,
        this.expandedContentSpacing = kDefaultSpacing,
        this.expandedContent,
        this.isChild = false})
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
          header: _header,
          headerHeight: 72,
          trailing: _trailing,
          content: _content
      ),
    );
  }

  Widget get _content {
    var contents = widget.expandedContent!;
    var items = List.generate(contents.length * 2, (index) => index % 2 == 0 ? contents[index ~/ 2] : SizedBox(height: widget.expandedContentSpacing));
    return Column(
        children: items
    );
  }

  Widget get _trailing => SizedBox(
      width: widget.contentWidth,
      child: widget.content
  );

  Widget get _header => ListTile(
      title: Text(widget.title),
      subtitle: Text(widget.subtitle)
  );

  Widget get _contentCard {
    if (widget.isChild) {
      return Padding(
          padding: const EdgeInsets.symmetric(vertical: 8.0),
          child: _contentCardBody
      );
    }

    return FluentCard(
      child: _contentCardBody,
    );
  }

  Widget get _contentCardBody => ListTile(
      title: Text(widget.title),
      subtitle: Text(widget.subtitle),
      trailing: _trailing
  );
}