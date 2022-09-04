import 'package:fluent_ui/fluent_ui.dart';

import '../model/fortnite_build.dart';
import '../util/generic_controller.dart';

class BuildSelector extends StatefulWidget {
  final List<FortniteBuild> builds;
  final GenericController<FortniteBuild?> controller;

  const BuildSelector(
      {required this.builds, required this.controller, Key? key})
      : super(key: key);

  @override
  State<BuildSelector> createState() => _BuildSelectorState();
}

class _BuildSelectorState extends State<BuildSelector> {
  String? value;

  @override
  Widget build(BuildContext context) {
    widget.controller.value = widget.controller.value ?? widget.builds[0];
    return InfoLabel(
      label: "Build",
      child: Combobox<FortniteBuild>(
          placeholder: const Text('Select a fortnite build'),
          isExpanded: true,
          items: _createItems(),
          value: widget.controller.value,
          onChanged: (value) => value == null ? {} : setState(() => widget.controller.value = value)
      ),
    );
  }

  List<ComboboxItem<FortniteBuild>> _createItems() {
    return widget.builds.map((element) => _createItem(element)).toList();
  }

  ComboboxItem<FortniteBuild> _createItem(FortniteBuild element) {
    return ComboboxItem<FortniteBuild>(
      value: element,
      child: Text("${element.version} ${element.hasManifest ? '[Fortnite Manifest]' : '[Google Drive]'}"),
    );
  }
}
