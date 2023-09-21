import 'package:auto_animated_list/auto_animated_list.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:skeletons/skeletons.dart';

class SettingTile extends StatefulWidget {
  static const double kDefaultContentWidth = 200.0;
  static const double kDefaultHeaderHeight = 72;

  final String? title;
  final TextStyle? titleStyle;
  final dynamic subtitle;
  final TextStyle? subtitleStyle;
  final Widget? content;
  final double? contentWidth;
  final List<Widget>? expandedContent;
  final double expandedContentHeaderHeight;
  final bool isChild;

  const SettingTile(
      {Key? key,
        this.title,
        this.titleStyle,
        this.subtitle,
        this.subtitleStyle,
        this.content,
        this.contentWidth = kDefaultContentWidth,
        this.expandedContentHeaderHeight = kDefaultHeaderHeight,
        this.expandedContent,
        this.isChild = false})
      : assert((title == null && subtitle == null) || (title != null && subtitle != null), "title and subtitle can only be null together"),
        assert(subtitle == null || subtitle is String || subtitle is Widget, "subtitle can only be null, String or Widget"),
        assert(subtitle is! Widget || subtitleStyle == null, "subtitleStyle must be null if subtitle is a widget"),
        super(key: key);

  @override
  State<SettingTile> createState() => _SettingTileState();
}

class _SettingTileState extends State<SettingTile> {
  @override
  Widget build(BuildContext context) {
    return ConstrainedBox(
        constraints: const BoxConstraints(
            maxWidth: 1000
        ),
        child: () {
          if (widget.expandedContent == null || widget.expandedContent?.isEmpty == true) {
            return _contentCard;
          }

          return Expander(
              initiallyExpanded: true,
              headerShape: (open) => const RoundedRectangleBorder(
                borderRadius: BorderRadius.vertical(top: Radius.circular(4.0)),
              ),
              header: SizedBox(
                  height: widget.expandedContentHeaderHeight,
                  child: _buildTile(false)
              ),
              trailing: _trailing,
              content: _expandedContent
          );
        }()
    );
  }

  Widget get _expandedContent {
    var expandedContents = widget.expandedContent!;
    var separatedContents = List.generate(expandedContents.length, (index) => expandedContents[index]);
    return AutoAnimatedList<Widget>(
        scrollDirection: Axis.vertical,
        shrinkWrap: true,
        items: separatedContents,
        itemBuilder: (context, child, index, animation) => FadeTransition(
            opacity: animation,
            child: child
        )
    );
  }

  Widget get _trailing =>
      SizedBox(width: widget.contentWidth, child: widget.content);

  Widget get _contentCard {
    if (widget.isChild) {
      return Padding(
          padding: const EdgeInsets.symmetric(vertical: 8.0),
          child: _buildTile(true)
      );
    }

    return Card(
        borderRadius: const BorderRadius.vertical(top: Radius.circular(4.0)),
        child: _buildTile(true)
    );
  }

  Widget _buildTile(bool trailing) {
    return ListTile(
        title: widget.title == null ? _skeletonTitle : _title,
        subtitle: widget.title == null ? _skeletonSubtitle : _subtitle,
        trailing: trailing ? _trailing : null
    );
  }

  Widget get _title => Text(
    widget.title!,
    style:
    widget.titleStyle ?? FluentTheme.of(context).typography.subtitle
  );

  Widget get _skeletonTitle => const SkeletonLine(
    style: SkeletonLineStyle(
        padding: EdgeInsets.only(
            right: 24.0
        ),
        height: 18
    ),
  );

  Widget get _subtitle => widget.subtitle is Widget ? widget.subtitle : Text(
      widget.subtitle!,
      style: widget.subtitleStyle ?? FluentTheme.of(context).typography.body
  );

  Widget get _skeletonSubtitle => const SkeletonLine(
      style: SkeletonLineStyle(
          padding: EdgeInsets.only(
              top: 8.0,
              bottom: 8.0,
              right: 24.0
          ),
          height: 13
      )
  );
}
