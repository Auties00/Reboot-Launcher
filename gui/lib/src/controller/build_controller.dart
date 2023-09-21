import 'package:get/get.dart';
import 'package:reboot_common/common.dart';

class BuildController extends GetxController {
  List<FortniteBuild>? _builds;
  Rxn<FortniteBuild> selectedBuild;

  BuildController() : selectedBuild = Rxn();

  List<FortniteBuild>? get builds => _builds;

  set builds(List<FortniteBuild>? builds) {
    _builds = builds;
    if(builds == null || builds.isEmpty){
      return;
    }
    selectedBuild.value = builds[0];
  }
}
