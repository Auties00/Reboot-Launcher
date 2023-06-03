import 'package:get/get.dart';
import 'package:reboot_launcher/src/model/fortnite_build.dart';

class BuildController extends GetxController {
  List<FortniteBuild>? _builds;
  Rxn<FortniteBuild> selectedBuildRx;

  BuildController() : selectedBuildRx = Rxn();

  List<FortniteBuild>? get builds => _builds;

  set builds(List<FortniteBuild>? builds) {
    _builds = builds;
    if(builds == null || builds.isEmpty){
      return;
    }
    selectedBuildRx.value = builds[0];
  }
}
