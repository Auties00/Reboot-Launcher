import 'package:get/get.dart';
import 'package:reboot_launcher/src/model/fortnite_build.dart';

class BuildController extends GetxController {
  List<FortniteBuild>? builds;
  FortniteBuild? _selectedBuild;
  final List<Function()> _listeners;
  late RxBool cancelledDownload;

  BuildController() : _listeners = [] {
    cancelledDownload = RxBool(false);
  }

  FortniteBuild get selectedBuild => _selectedBuild ?? builds!.elementAt(0);

  set selectedBuild(FortniteBuild build) {
    _selectedBuild = build;
    for (var listener in _listeners) {
      listener();
    }
  }

  void addOnBuildChangedListener(Function() listener) => _listeners.add(listener);

  void removeOnBuildChangedListener() => _listeners.clear();
}
