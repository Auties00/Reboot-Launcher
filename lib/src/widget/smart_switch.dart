import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:system_theme/system_theme.dart';

class SmartSwitch extends StatefulWidget {
  final String? label;
  final bool enabled;
  final Function()? onDisabledPress;
  final Rx<bool> value;

  const SmartSwitch(
      {Key? key,
        required this.value,
        this.label,
        this.enabled = true,
        this.onDisabledPress})
      : super(key: key);

  @override
  State<SmartSwitch> createState() => _SmartSwitchState();
}

class _SmartSwitchState extends State<SmartSwitch> {
  @override
  Widget build(BuildContext context) {
    return widget.label == null ?  _createSwitch() : _createLabel();
  }

  InfoLabel _createLabel() {
    return InfoLabel(
        label: widget.label!,
        child: _createSwitch()
    );
  }

  Widget _createSwitch() {
    return Obx(() => ToggleSwitch(
        enabled: widget.enabled,
        onDisabledPress: widget.onDisabledPress,
        checked: widget.value.value,
        onChanged: _onChanged,
        style: ToggleSwitchThemeData.standard(ThemeData(
            checkedColor: _toolTipColor.withOpacity(_checkedOpacity),
            uncheckedColor: _toolTipColor.withOpacity(_uncheckedOpacity),
            borderInputColor: _toolTipColor.withOpacity(_uncheckedOpacity),
            accentColor: _bodyColor
                .withOpacity(widget.value.value
                ? _checkedOpacity
                : _uncheckedOpacity)
                .toAccentColor())))
    );
  }

  Color get _toolTipColor =>
      FluentTheme.of(context).brightness.isDark ? Colors.white : Colors.black;

  Color get _bodyColor => SystemTheme.accentColor.accent;

  double get _checkedOpacity => widget.enabled ? 1 : 0.5;

  double get _uncheckedOpacity => widget.enabled ? 0.8 : 0.5;

  void _onChanged(bool checked) {
    if (!widget.enabled) {
      return;
    }

    setState(() => widget.value.value = checked);
  }
}
