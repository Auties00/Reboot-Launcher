import 'package:fluent_ui/fluent_ui.dart';
import 'package:shared_preferences/shared_preferences.dart';

class SmartInput extends StatefulWidget {
  final String keyName;
  final String label;
  final String placeholder;
  final TextEditingController controller;
  final TextInputType type;
  final bool enabled;
  final VoidCallback? onTap;
  final bool populate;

  const SmartInput(
      {Key? key,
      required this.keyName,
      required this.label,
      required this.placeholder,
      required this.controller,
      this.onTap,
      this.enabled = true,
      this.populate = false,
      this.type = TextInputType.text})
      : super(key: key);

  @override
  State<SmartInput> createState() => _SmartInputState();
}

class _SmartInputState extends State<SmartInput> {
  @override
  Widget build(BuildContext context) {
    return widget.populate ? _buildPopulatedTextBox() : _buildTextBox();
  }

  FutureBuilder _buildPopulatedTextBox(){
    return FutureBuilder(
        future: SharedPreferences.getInstance(),
        builder: (context, snapshot) {
          _update(snapshot.data);
          return _buildTextBox();
        }
    );
  }

  void _update(SharedPreferences? preferences) {
    if(preferences == null){
      return;
    }

    var decoded = preferences.getString(widget.keyName);
    if(decoded == null) {
      return;
    }

    widget.controller.text = decoded;
  }

  TextBox _buildTextBox() {
    return TextBox(
      enabled: widget.enabled,
      controller: widget.controller,
      header: widget.label,
      keyboardType: widget.type,
      placeholder: widget.placeholder,
      onChanged: _save,
      onTap: widget.onTap,
    );
  }

  Future<void> _save(String value) async {
    final preferences = await SharedPreferences.getInstance();
    preferences.setString(widget.keyName, value);
  }
}
