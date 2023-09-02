import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';

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
            value: _buildController.selectedBuild.value,
            onChanged: (value) {
              if(value == null){
                return;
              }

              _buildController.selectedBuild.value = value;
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
