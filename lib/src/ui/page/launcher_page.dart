import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/page/browse_page.dart';
import 'package:reboot_launcher/src/ui/page/play_page.dart';


class LauncherPage extends StatefulWidget {
  final GlobalKey<NavigatorState> navigatorKey;
  final RxInt nestedNavigation;
  const LauncherPage(this.navigatorKey, this.nestedNavigation, {Key? key}) : super(key: key);

  @override
  State<LauncherPage> createState() => _LauncherPageState();
}

class _LauncherPageState extends State<LauncherPage> with AutomaticKeepAliveClientMixin {
  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Navigator(
      key: widget.navigatorKey,
      initialRoute: "home",
      onGenerateRoute: (settings) {
        var screen = _createScreen(settings.name);
        return FluentPageRoute(
            builder: (context) => screen,
            settings: settings
        );
      },
    );
  }

  Widget _createScreen(String? name) {
    switch(name){
      case "home":
        return PlayPage(widget.navigatorKey, widget.nestedNavigation);
      case "browse":
        return const BrowsePage();
      default:
        throw Exception("Unknown page: $name");
    }
  }
}