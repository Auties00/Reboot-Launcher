import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/controller/build_controller.dart';
import 'package:reboot_launcher/src/model/fortnite_build.dart';

class BuildSelector extends StatefulWidget {
  final Function() onSelected;

  const BuildSelector({Key? key, required this.onSelected}) : super(key: key);

  @override
  State<BuildSelector> createState() => _BuildSelectorState();
}

class _BuildSelectorState extends State<BuildSelector> {
  final BuildController _buildController = Get.find<BuildController>();

  @override
  Widget build(BuildContext context) {
    return InfoLabel(
        label: "Build",
        child: Obx(() => ComboBox<FortniteBuild>(
            placeholder: const Text('Select a fortnite build'),
            isExpanded: true,
            items: _createItems(),
            value: _buildController.selectedBuildRx.value,
            onChanged: (value) {
              if(value == null){
                return;
              }

              _buildController.selectedBuildRx.value = value;
              widget.onSelected();
            }
        ))
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
        child: Text(element.version.toString())
    );
  }
}
