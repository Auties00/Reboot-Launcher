import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:skeletons/skeletons.dart';

class SettingTile extends StatelessWidget {
  static const double kDefaultContentWidth = 200.0;
  static const double kDefaultHeaderHeight = 72;

  final void Function()? onPressed;
  final Icon icon;
  final Text? title;
  final Text? subtitle;
  final Widget? content;
  final double? contentWidth;
  final List<Widget>? children;

  const SettingTile({
    this.onPressed,
    required this.icon,
    required this.title,
    required this.subtitle,
    this.content,
    this.contentWidth = kDefaultContentWidth,
    this.children
  });
  
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(
          bottom: 4.0
      ),
      child: HoverButton(
        onPressed: _buildOnPressed(context),
        builder: (context, states) => Container(
            height: 80,
            width: double.infinity,
            decoration: BoxDecoration(
                color: ButtonThemeData.uncheckedInputColor(
                  FluentTheme.of(context),
                  states,
                  transparentWhenNone: true,
                ),
                borderRadius: BorderRadius.all(Radius.circular(4.0))
            ),
            child: Card(
                borderRadius: const BorderRadius.all(
                    Radius.circular(4.0)
                ),
                child: Padding(
                  padding: const EdgeInsetsDirectional.symmetric(
                      horizontal: 12.0,
                      vertical: 6.0
                  ),
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.center,
                    children: [
                      icon,
                      const SizedBox(width: 16.0),
                      Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          title == null ? _skeletonTitle : title!,
                          subtitle == null ? _skeletonSubtitle : subtitle!,
                        ],
                      ),
                      const Spacer(),
                      _trailing
                    ],
                  ),
                )
            )
        ),
      ),
    );
  }

  void Function()? _buildOnPressed(BuildContext context) {
    if(onPressed != null) {
      return onPressed;
    }

    final children = this.children;
    if (children == null) {
      return null;
    }

    return () async {
      await Navigator.of(context).push(PageRouteBuilder(
          transitionDuration: Duration.zero,
          reverseTransitionDuration: Duration.zero,
          settings: RouteSettings(
              name: title?.data
          ),
          pageBuilder: (context, incoming, outgoing) => ListView.builder(
              itemCount: children.length,
              itemBuilder: (context, index) => children[index]
          )
      ));
      WidgetsBinding.instance.addPostFrameCallback((timeStamp) => pageIndex.value = pageIndex.value);
    };
  }

  Widget get _trailing {
    final hasContent = content != null;
    final hasChildren = children?.isNotEmpty == true;
    final hasListener = onPressed != null;
    if(hasContent && hasChildren) {
      return Row(
        children: [
          SizedBox(
              width: contentWidth,
              child: content
          ),
          const SizedBox(width: 16.0),
          Icon(
              FluentIcons.chevron_right_24_regular
          )
        ],
      );
    }

    if (hasContent) {
      return SizedBox(
          width: contentWidth,
          child: content
      );
    }

    if (hasChildren || hasListener) {
      return Icon(
          FluentIcons.chevron_right_24_regular
      );
    }

    return const SizedBox.shrink();
  }

  Widget get _skeletonTitle => const SkeletonLine(
    style: SkeletonLineStyle(
        padding: EdgeInsets.only(
            right: 24.0
        ),
        height: 18
    ),
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
