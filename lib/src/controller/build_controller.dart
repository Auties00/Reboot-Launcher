import 'package:get/get.dart';

import 'package:reboot_launcher/src/model/fortnite_build.dart';

class BuildController extends GetxController {
  List<FortniteBuild>? builds;
  FortniteBuild? _selectedBuild;
  late RxBool cancelledDownload;

  BuildController() {
    cancelledDownload = RxBool(false);
  }

  FortniteBuild get selectedBuild => _selectedBuild ?? builds!.elementAt(0);

  set selectedBuild(FortniteBuild build) => _selectedBuild = build;
}
