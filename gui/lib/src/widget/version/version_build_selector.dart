import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/util/translations.dart';

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
        label: translations.build,
        child: Obx(() => ComboBox<FortniteBuild>(
            placeholder: Text(translations.selectBuild),
            isExpanded: true,
            items: _items,
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

  List<ComboBoxItem<FortniteBuild>> get _items =>_buildController.builds!
      .map((element) => _buildItem(element))
      .toList();

  ComboBoxItem<FortniteBuild> _buildItem(FortniteBuild element) {
    return ComboBoxItem<FortniteBuild>(
        value: element,
        child: Text(element.version.toString())
    );
  }
}
