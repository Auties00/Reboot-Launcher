import 'package:fluent_ui/fluent_ui.dart';

class RebootPaneItem extends PaneItem {
  RebootPaneItem({required super.title, required super.icon, required super.body});

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
    final maybeBody = _InheritedNavigationView.maybeOf(context);
    final mode = displayMode ??
        maybeBody?.displayMode ??
        maybeBody?.pane?.displayMode ??
        PaneDisplayMode.minimal;
    assert(mode != PaneDisplayMode.auto);
    assert(debugCheckHasFluentTheme(context));

    final isTransitioning = maybeBody?.isTransitioning ?? false;

    final theme = NavigationPaneTheme.of(context);
    final titleText = title?.getProperty<String>() ?? '';

    final baseStyle = title?.getProperty<TextStyle>() ?? const TextStyle();

    final isTop = mode == PaneDisplayMode.top;
    final isMinimal = mode == PaneDisplayMode.minimal;
    final isCompact = mode == PaneDisplayMode.compact;

    final onItemTapped =
        (onPressed == null && onTap == null) || !enabled || isTransitioning
            ? null
            : () {
                onPressed?.call();
                onTap?.call();
              };

    final button = HoverButton(
      autofocus: autofocus ?? this.autofocus,
      focusNode: focusNode,
      onPressed: onItemTapped,
      cursor: mouseCursor,
      focusEnabled: isMinimal ? (maybeBody?.minimalPaneOpen ?? false) : true,
      forceEnabled: enabled,
      builder: (context, states) {
        var textStyle = () {
          var style = !isTop
              ? (selected
                  ? theme.selectedTextStyle?.resolve(states)
                  : theme.unselectedTextStyle?.resolve(states))
              : (selected
                  ? theme.selectedTopTextStyle?.resolve(states)
                  : theme.unselectedTopTextStyle?.resolve(states));
          if (style == null) return baseStyle;
          return style.merge(baseStyle);
        }();

        final textResult = titleText.isNotEmpty
            ? Padding(
                padding: theme.labelPadding ?? EdgeInsets.zero,
                child: RichText(
                  text: title!.getProperty<InlineSpan>(textStyle)!,
                  maxLines: 1,
                  overflow: TextOverflow.fade,
                  softWrap: false,
                  textAlign: title?.getProperty<TextAlign>() ?? TextAlign.start,
                  textHeightBehavior: title?.getProperty<TextHeightBehavior>(),
                  textWidthBasis: title?.getProperty<TextWidthBasis>() ??
                      TextWidthBasis.parent,
                ),
              )
            : const SizedBox.shrink();
        Widget result() {
          final iconThemeData = IconThemeData(
            color: textStyle.color ??
                (selected
                    ? theme.selectedIconColor?.resolve(states)
                    : theme.unselectedIconColor?.resolve(states)),
            size: textStyle.fontSize ?? 16.0,
          );
          switch (mode) {
            case PaneDisplayMode.compact:
              return Container(
                key: itemKey,
                constraints: const BoxConstraints(
                  minHeight: kPaneItemMinHeight,
                ),
                alignment: AlignmentDirectional.center,
                child: Padding(
                  padding: theme.iconPadding ?? EdgeInsets.zero,
                  child: IconTheme.merge(
                    data: iconThemeData,
                    child: Align(
                      alignment: AlignmentDirectional.centerStart,
                      child: () {
                        if (infoBadge != null) {
                          return Stack(
                            alignment: AlignmentDirectional.center,
                            clipBehavior: Clip.none,
                            children: [
                              icon,
                              PositionedDirectional(
                                end: -8,
                                top: -8,
                                child: infoBadge!,
                              ),
                            ],
                          );
                        }
                        return icon;
                      }(),
                    ),
                  ),
                ),
              );
            case PaneDisplayMode.minimal:
            case PaneDisplayMode.open:
              final shouldShowTrailing = !isTransitioning;

              return ConstrainedBox(
                key: itemKey,
                constraints: const BoxConstraints(
                  minHeight: kPaneItemMinHeight,
                ),
                child: Row(children: [
                  Padding(
                    padding: theme.iconPadding ?? EdgeInsets.zero,
                    child: IconTheme.merge(
                      data: iconThemeData,
                      child: Center(child: icon),
                    ),
                  ),
                  Expanded(child: textResult),
                  if (shouldShowTrailing) ...[
                    if (infoBadge != null)
                      Padding(
                        padding: const EdgeInsetsDirectional.only(end: 8.0),
                        child: infoBadge!,
                      ),
                    if (trailing != null)
                      IconTheme.merge(
                        data: const IconThemeData(size: 16.0),
                        child: trailing!,
                      ),
                  ],
                ]),
              );
            case PaneDisplayMode.top:
              Widget result = Row(mainAxisSize: MainAxisSize.min, children: [
                Padding(
                  padding: theme.iconPadding ?? EdgeInsets.zero,
                  child: IconTheme.merge(
                    data: iconThemeData,
                    child: Center(child: icon),
                  ),
                ),
                if (showTextOnTop) textResult,
                if (trailing != null)
                  IconTheme.merge(
                    data: const IconThemeData(size: 16.0),
                    child: trailing!,
                  ),
              ]);
              if (infoBadge != null) {
                return Stack(key: itemKey, clipBehavior: Clip.none, children: [
                  result,
                  if (infoBadge != null)
                    PositionedDirectional(
                      end: -3,
                      top: 3,
                      child: infoBadge!,
                    ),
                ]);
              }
              return KeyedSubtree(key: itemKey, child: result);
            default:
              throw '$mode is not a supported type';
          }
        }

        return Semantics(
          label: titleText.isEmpty ? null : titleText,
          selected: selected,
          child: Container(
            margin: const EdgeInsets.symmetric(horizontal: 6.0),
            decoration: BoxDecoration(
              color: () {
                final tileColor = this.tileColor ??
                    theme.tileColor ??
                    kDefaultPaneItemColor(context, isTop);
                final newStates = states.toSet()..remove(ButtonStates.disabled);
                if (selected && selectedTileColor != null) {
                  return selectedTileColor!.resolve(newStates);
                }
                return tileColor.resolve(
                  selected
                      ? {
                          states.isHovering
                              ? ButtonStates.pressing
                              : ButtonStates.hovering,
                        }
                      : newStates,
                );
              }(),
              borderRadius: BorderRadius.circular(4.0),
            ),
            child: FocusBorder(
              focused: states.isFocused,
              renderOutside: false,
              child: () {
                final showTooltip = ((isTop && !showTextOnTop) || isCompact) &&
                    titleText.isNotEmpty &&
                    !states.isDisabled;

                if (showTooltip) {
                  return Tooltip(
                    richMessage: title?.getProperty<InlineSpan>(),
                    style: TooltipThemeData(textStyle: baseStyle),
                    child: result(),
                  );
                }

                return result();
              }(),
            ),
          ),
        );
      },
    );

    final index = () {
      if (itemIndex != null) return itemIndex;
      if (maybeBody?.pane?.indicator != null) {
        return maybeBody!.pane!.effectiveIndexOf(this);
      }
    }();

    return Padding(
      key: key,
      padding: const EdgeInsetsDirectional.symmetric(horizontal: 12.0, vertical: 2.0),
      child: () {
        if (maybeBody?.pane?.indicator != null &&
            index != null &&
            !index.isNegative) {
          final key = PaneItemKeys.of(index, context);

          return Stack(children: [
            button,
            Positioned.fill(
              child: _InheritedNavigationView.merge(
                currentItemIndex: index,
                currentItemSelected: selected,
                child: KeyedSubtree(
                  key: key,
                  child: maybeBody!.pane!.indicator!,
                ),
              ),
            ),
          ]);
        }

        return button;
      }(),
    );
  }
}

class _InheritedNavigationView extends InheritedWidget {
  const _InheritedNavigationView({
    super.key,
    required super.child,
    required this.displayMode,
    this.minimalPaneOpen = false,
    this.pane,
    this.previousItemIndex = 0,
    this.currentItemIndex = -1,
    this.isTransitioning = false,
  });

  final PaneDisplayMode displayMode;

  final bool minimalPaneOpen;

  final NavigationPane? pane;

  final int previousItemIndex;

  final int currentItemIndex;

  final bool isTransitioning;

  static _InheritedNavigationView? maybeOf(BuildContext context) {
    return context
        .dependOnInheritedWidgetOfExactType<_InheritedNavigationView>();
  }

  static Widget merge({
    Key? key,
    required Widget child,
    int? currentItemIndex,
    NavigationPane? pane,
    PaneDisplayMode? displayMode,
    bool? minimalPaneOpen,
    int? previousItemIndex,
    bool? currentItemSelected,
    bool? isTransitioning,
  }) {
    return Builder(builder: (context) {
      final current = _InheritedNavigationView.maybeOf(context);
      return _InheritedNavigationView(
        key: key,
        displayMode:
            displayMode ?? current?.displayMode ?? PaneDisplayMode.open,
        minimalPaneOpen: minimalPaneOpen ?? current?.minimalPaneOpen ?? false,
        currentItemIndex: currentItemIndex ?? current?.currentItemIndex ?? -1,
        pane: pane ?? current?.pane,
        previousItemIndex: previousItemIndex ?? current?.previousItemIndex ?? 0,
        isTransitioning: isTransitioning ?? current?.isTransitioning ?? false,
        child: child,
      );
    });
  }

  @override
  bool updateShouldNotify(covariant _InheritedNavigationView oldWidget) {
    return oldWidget.displayMode != displayMode ||
        oldWidget.minimalPaneOpen != minimalPaneOpen ||
        oldWidget.pane != pane ||
        oldWidget.previousItemIndex != previousItemIndex ||
        oldWidget.currentItemIndex != currentItemIndex ||
        oldWidget.isTransitioning != isTransitioning;
  }
}
