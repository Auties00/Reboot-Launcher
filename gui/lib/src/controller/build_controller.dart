import 'package:get/get.dart';
import 'package:reboot_common/common.dart';

class BuildController extends GetxController {
  List<FortniteBuild>? _builds;
  Rxn<FortniteBuild> _selectedBuild;
  Rx<FortniteBuildSource> _selectedBuildSource;

  BuildController() : _selectedBuild = Rxn(),
        _selectedBuildSource = Rx(FortniteBuildSource.manifest);

  List<FortniteBuild>? get builds => _builds;

  FortniteBuild? get selectedBuild => _selectedBuild.value;

  set selectedBuild(FortniteBuild? value) {
    _selectedBuild.value = value;
    if(value != null && value.source != value.source) {
      _selectedBuildSource.value = value.source;
    }
  }

  FortniteBuildSource get selectedBuildSource => _selectedBuildSource.value;

  set selectedBuildSource(FortniteBuildSource value) {
    _selectedBuildSource.value = value;
    final selected = selectedBuild;
    if(selected == null || selected.source != value) {
      final selectable = builds?.firstWhereOrNull((element) => element.source == value);
      _selectedBuild.value = selectable;
    }
  }


  set builds(List<FortniteBuild>? builds) {
    _builds = builds;
    final selectable = builds?.firstWhereOrNull((element) => element.source == selectedBuildSource);
    _selectedBuild.value = selectable;
  }
}
