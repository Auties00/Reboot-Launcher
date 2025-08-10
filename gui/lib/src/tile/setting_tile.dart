import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:fluentui_system_icons/fluentui_system_icons.dart';
import 'package:reboot_launcher/src/messenger/overlay.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:skeletons/skeletons.dart';

class SettingTile extends StatefulWidget {
  static const double kDefaultHeight = 80.0;
  static const double kDefaultContentWidth = 200.0;

  final void Function()? onPressed;
  final Icon? icon;
  final Text? title;
  final Text? subtitle;
  final Widget? content;
  final double? contentWidth;
  final Key? overlayKey;
  final List<Widget>? children;

  const SettingTile({
    super.key,
    this.icon,
    this.title,
    this.subtitle,
    this.onPressed,
    this.content,
    this.contentWidth = kDefaultContentWidth,
    this.overlayKey,
    this.children
  });

  @override
  State<SettingTile> createState() => SettingTileState();
}

class SettingTileState extends State<SettingTile> {
  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(
        bottom: 4.0
    ),
    child: HoverButton(
      onPressed: _buildOnPressed(),
      builder: (context, states) => ConstrainedBox(
        constraints: BoxConstraints(
            minHeight: SettingTile.kDefaultHeight
        ),
        child: Container(
            width: double.infinity,
            decoration: BoxDecoration(
                color: ButtonThemeData.uncheckedInputColor(
                  FluentTheme.of(context),
                  states,
                  transparentWhenNone: true,
                ),
                borderRadius: BorderRadius.all(Radius.circular(6.0))
            ),
            child: _buildBody()
        ),
      ),
    ),
  );

  Card _buildBody() {
    final icon = widget.icon;
    final title = widget.title;
    final subtitle = widget.subtitle;
    final isSkeleton = icon == null || title == null || subtitle == null;
    return Card(
        borderRadius: const BorderRadius.all(
            Radius.circular(6.0)
        ),
        child: Padding(
          padding: const EdgeInsetsDirectional.symmetric(
              horizontal: 12.0
          ),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              if(widget.overlayKey != null)
                OverlayTarget(
                  key: widget.overlayKey,
                  child: isSkeleton ? _skeletonIcon : icon,
                )
              else
                isSkeleton ? _skeletonIcon : icon,

              const SizedBox(width: 16.0),

              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    isSkeleton ? _skeletonTitle : title,
                    isSkeleton ? _skeletonSubtitle : subtitle
                  ],
                ),
              ),

              const SizedBox(width: 16.0),

              _trailing
            ],
          ),
        )
    );
  }

  SkeletonAvatar get _skeletonIcon => const SkeletonAvatar(style: SkeletonAvatarStyle(
      width: 30,
      height: 30,
      shape: BoxShape.circle
  ));

  void Function()? _buildOnPressed() {
    if(widget.onPressed != null) {
      return widget.onPressed;
    }

    final children = this.widget.children;
    if (children == null) {
      return null;
    }

    return () => openNestedPage();
  }

  Future<void> openNestedPage() async {
    final children = this.widget.children;
    if (children == null) {
      return;
    }

    await Navigator.of(context).push(PageRouteBuilder(
        transitionDuration: Duration.zero,
        reverseTransitionDuration: Duration.zero,
        settings: RouteSettings(
            name: widget.title?.data
        ),
        pageBuilder: (context, incoming, outgoing) => ListView.builder(
            itemCount: children.length,
            itemBuilder: (context, index) => children[index]
        )
    ));
    WidgetsBinding.instance.addPostFrameCallback((timeStamp) => pageIndex.value = pageIndex.value);
  }

  Widget get _trailing {
    final hasContent = widget.content != null;
    final hasChildren = widget.children?.isNotEmpty == true;
    final hasListener = widget.onPressed != null;
    if(hasContent && hasChildren) {
      return Row(
        children: [
          SizedBox(
              width: widget.contentWidth,
              child: widget.content
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
          width: widget.contentWidth,
          child: widget.content
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
