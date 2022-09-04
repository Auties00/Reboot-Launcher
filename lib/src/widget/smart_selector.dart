import 'package:fluent_ui/fluent_ui.dart';
import 'package:shared_preferences/shared_preferences.dart';

class SmartSelector extends StatefulWidget {
  final String keyName;
  final String? label;
  final String placeholder;
  final List<String> options;
  final SmartSelectorItem Function(String)? itemBuilder;
  final Function(String)? onSelected;
  final bool serializer;
  final String? initialValue;
  final bool enabled;
  final bool useFirstItemByDefault;

  const SmartSelector({Key? key,
    required this.keyName,
    required this.placeholder,
    required this.options,
    required this.initialValue,
    this.itemBuilder,
    this.onSelected,
    this.label,
    this.serializer = true,
    this.enabled = true,
    this.useFirstItemByDefault = true})
      : super(key: key);

  @override
  State<SmartSelector> createState() => _SmartSelectorState();
}

class _SmartSelectorState extends State<SmartSelector> {
  String? _selected;

  @override
  void initState() {
    _selected = widget.initialValue;
    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    return widget.label == null ? _buildBody() : _buildLabel();
  }

  InfoLabel _buildLabel() {
    return InfoLabel(label: widget.label!, child: _buildBody());
  }

  SizedBox _buildBody() {
    return SizedBox(
      width: double.infinity,
      child: DropDownButton(
        leading: Text(_selected ?? widget.placeholder),
        items: widget.options.map(_createOption).toList()
      ),
    );
  }

  MenuFlyoutItem _createOption(String option) {
    var function = widget.itemBuilder ?? _createDefaultItem;
    var item = function(option);
    return MenuFlyoutItem(
        key: item.key,
        text: item.text,
        onPressed: () => widget.enabled && item.clickable ? _onSelected(option) : {},
        leading: item.leading,
        trailing: item.trailing,
        selected: item.selected
    );
  }

  SmartSelectorItem _createDefaultItem(String name) {
    return SmartSelectorItem(
        text: SizedBox(width: double.infinity, child: Text(name)));
  }

  void _onSelected(String name) {
    setState(() {
      widget.onSelected?.call(name);
      _selected = name;
      if(!widget.serializer){
        return;
      }

      _serialize(name);
    });
  }

  Future<void> _serialize(String value) async {
    final preferences = await SharedPreferences.getInstance();
    preferences.setString(widget.keyName, value);
  }
}

class SmartSelectorItem {
  final Key? key;
  final Widget? leading;
  final Widget text;
  final Widget? trailing;
  final bool selected;
  final bool clickable;

  SmartSelectorItem({this.key,
    this.leading,
    required this.text,
    this.trailing,
    this.selected = false,
    this.clickable = true});
}
