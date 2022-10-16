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
        checked: widget.value.value,
        onChanged: _onChanged
    )
    );
  }
  void _onChanged(bool checked) {
    if (!widget.enabled) {
      return;
    }

    setState(() => widget.value.value = checked);
  }
}
