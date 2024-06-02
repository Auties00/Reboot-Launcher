import 'package:get/get.dart';
import 'package:reboot_common/common.dart';

class BuildController extends GetxController {
  List<FortniteBuild>? _builds;
  Rxn<FortniteBuild> _selectedBuild;

  BuildController() : _selectedBuild = Rxn();

  List<FortniteBuild>? get builds => _builds;

  FortniteBuild? get selectedBuild => _selectedBuild.value;

  set selectedBuild(FortniteBuild? value) {
    _selectedBuild.value = value;
  }

  set builds(List<FortniteBuild>? builds) {
    _builds = builds;
    _selectedBuild.value = builds?.firstOrNull;
  }
}
