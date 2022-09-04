import 'package:fluent_ui/fluent_ui.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:system_theme/system_theme.dart';

import '../util/generic_controller.dart';

class SmartSwitch extends StatefulWidget {
  final String keyName;
  final String label;
  final bool enabled;
  final Function(bool)? onSelected;
  final Function()? onDisabledPress;
  final GenericController<bool> controller;

  const SmartSwitch(
      {Key? key,
      required this.keyName,
      required this.label,
      required this.controller,
      this.onSelected,
      this.enabled = true,
      this.onDisabledPress})
      : super(key: key);

  @override
  State<SmartSwitch> createState() => _SmartSwitchState();
}

class _SmartSwitchState extends State<SmartSwitch> {
  Future<void> _save(bool state) async {
    final preferences = await SharedPreferences.getInstance();
    preferences.setBool(widget.keyName, state);
  }

  @override
  Widget build(BuildContext context) {
    return InfoLabel(
        label: widget.label,
        child: ToggleSwitch(
            enabled: widget.enabled,
            onDisabledPress: widget.onDisabledPress,
            checked: widget.controller.value,
            onChanged: _onChanged,
            style: ToggleSwitchThemeData.standard(ThemeData(
                checkedColor: _toolTipColor.withOpacity(_checkedOpacity),
                uncheckedColor: _toolTipColor.withOpacity(_uncheckedOpacity),
                borderInputColor: _toolTipColor.withOpacity(_uncheckedOpacity),
                accentColor: _bodyColor
                    .withOpacity(widget.controller.value
                        ? _checkedOpacity
                        : _uncheckedOpacity)
                    .toAccentColor()))));
  }

  Color get _toolTipColor =>
      FluentTheme.of(context).brightness.isDark ? Colors.white : Colors.black;

  Color get _bodyColor => SystemTheme.accentColor.accent;

  double get _checkedOpacity => widget.enabled ? 1 : 0.5;

  double get _uncheckedOpacity => widget.enabled ? 0.8 : 0.5;

  void _onChanged(checked) {
    if (!widget.enabled) {
      return;
    }

    setState(() {
      widget.controller.value = checked;
      widget.onSelected?.call(widget.controller.value);
      _save(checked);
    });
  }
}
