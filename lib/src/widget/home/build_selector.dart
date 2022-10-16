import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/model/fortnite_build.dart';

class BuildSelector extends StatefulWidget {

  const BuildSelector({Key? key}) : super(key: key);

  @override
  State<BuildSelector> createState() => _BuildSelectorState();
}

class _BuildSelectorState extends State<BuildSelector> {
  final BuildController _buildController = Get.find<BuildController>();

  @override
  Widget build(BuildContext context) {
    return InfoLabel(
        label: "Build",
        child: ComboBox<FortniteBuild>(
            placeholder: const Text('Select a fortnite build'),
            isExpanded: true,
            items: _createItems(),
            value: _buildController.selectedBuild,
            onChanged: (value) =>
            value == null ? {} : setState(() => _buildController.selectedBuild = value)
        )
    );
  }

  List<ComboBoxItem<FortniteBuild>> _createItems() {
    return _buildController.builds!
        .map((element) => _createItem(element))
        .toList();
  }

  ComboBoxItem<FortniteBuild> _createItem(FortniteBuild element) {
    return ComboBoxItem<FortniteBuild>(
      value: element,
      child: Text(
          "${element.version} ${element.hasManifest ? '[Fortnite Manifest]' : '[Google Drive]'}"),
    );
  }
}
