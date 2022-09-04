import 'package:fluent_ui/fluent_ui.dart';

import '../model/fortnite_version.dart';

class VersionNameInput extends StatelessWidget {
  final TextEditingController controller;
  final List<FortniteVersion> versions;
  const VersionNameInput({required this.controller, required this.versions, Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return TextFormBox(
      controller: controller,
      header: "Name",
      placeholder: "Type the version's name",
      autofocus: true,
      validator: _validate,
    );
  }

  String? _validate(String? text){
    if (text == null || text.isEmpty) {
      return 'Invalid version name';
    }

    if (versions.any((element) => element.name == text)) {
      return 'Existent game version';
    }

    return null;
  }
}
