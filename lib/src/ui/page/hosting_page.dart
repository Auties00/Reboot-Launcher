import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/ui/controller/settings_controller.dart';
import 'package:reboot_launcher/src/ui/widget/home/launch_button.dart';
import 'package:reboot_launcher/src/ui/widget/home/setting_tile.dart';
import 'package:reboot_launcher/src/ui/widget/home/version_selector.dart';

import 'package:reboot_launcher/src/model/update_status.dart';
import 'browse_page.dart';

class HostingPage extends StatefulWidget {
  final GlobalKey<NavigatorState> navigatorKey;
  final RxInt nestedNavigation;
  const HostingPage(this.navigatorKey, this.nestedNavigation, {Key? key}) : super(key: key);

  @override
  State<HostingPage> createState() => _HostingPageState();
}

class _HostingPageState extends State<HostingPage> with AutomaticKeepAliveClientMixin {
  final HostingController _hostingController = Get.find<HostingController>();
  final SettingsController _settingsController = Get.find<SettingsController>();

  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Obx(() => !_settingsController.autoUpdate() || _hostingController.updateStatus().isDone() ? _body : _updateScreen);
  }

  Widget get _updateScreen => const Row(
    mainAxisAlignment: MainAxisAlignment.center,
    children: [
      Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          ProgressRing(),
          SizedBox(height: 8.0),
          Text("Updating Reboot DLL...")
        ],
      ),
    ],
  );

  Widget get _body => Navigator(
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

  Widget _createScreen(String? name) {
    switch(name){
      case "home":
        return _HostPage(widget.navigatorKey, widget.nestedNavigation);
      case "browse":
        return const BrowsePage();
      default:
        throw Exception("Unknown page: $name");
    }
  }
}

class _HostPage extends StatefulWidget {
  final GlobalKey<NavigatorState> navigatorKey;
  final RxInt nestedNavigation;
  const _HostPage(this.navigatorKey, this.nestedNavigation, {Key? key}) : super(key: key);

  @override
  State<_HostPage> createState() => _HostPageState();
}

class _HostPageState extends State<_HostPage> with AutomaticKeepAliveClientMixin {
  final HostingController _hostingController = Get.find<HostingController>();

  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Column(
      children: [
        Expanded(
          child: ListView(
            children: [
              Obx(() => SizedBox(
                width: double.infinity,
                child: _hostingController.updateStatus.value == UpdateStatus.error ? _updateError : _rebootGuiInfo,
              )),
              const SizedBox(
                  height: 8.0
              ),
              SettingTile(
                title: "Game Server",
                subtitle: "Provide basic information about your server",
                expandedContentSpacing: 0,
                expandedContent: [
                  SettingTile(
                      title: "Name",
                      subtitle: "The name of your game server",
                      isChild: true,
                      content: TextFormBox(
                          placeholder: "Name",
                          controller: _hostingController.name
                      )
                  ),
                  SettingTile(
                      title: "Description",
                      subtitle: "The description of your game server",
                      isChild: true,
                      content: TextFormBox(
                          placeholder: "Description",
                          controller: _hostingController.description
                      )
                  ),
                  SettingTile(
                      title: "Discoverable",
                      subtitle: "Make your server available to other players on the server browser",
                      isChild: true,
                      contentWidth: null,
                      content: Obx(() => ToggleSwitch(
                          checked: _hostingController.discoverable(),
                          onChanged: (value) => _hostingController.discoverable.value = value
                      ))
                  ),
                ],
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                  title: "Version",
                  subtitle: "Select the version of Fortnite you want to host",
                  content: const VersionSelector(),
                  expandedContent: [
                    SettingTile(
                        title: "Add a version from this PC's local storage",
                        subtitle: "Versions coming from your local disk are not guaranteed to work",
                        content: Button(
                          onPressed: () => VersionSelector.openAddDialog(context),
                          child: const Text("Add build"),
                        ),
                        isChild: true
                    ),
                    SettingTile(
                        title: "Download any version from the cloud",
                        subtitle: "A curated list of supported versions by Project Reboot",
                        content: Button(
                          onPressed: () => VersionSelector.openDownloadDialog(context),
                          child: const Text("Download"),
                        ),
                        isChild: true
                    )
                  ]
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                  title: "Browse available servers",
                  subtitle: "See a list of other game servers that are being hosted",
                  content: Button(
                      onPressed: () {
                        widget.navigatorKey.currentState?.pushNamed('browse');
                        widget.nestedNavigation.value += 1;
                      },
                      child: const Text("Browse")
                  )
              ),
            ],
          ),
        ),
        const SizedBox(
          height: 8.0,
        ),
        const LaunchButton(
            host: true
        )
      ],
    );
  }

  InfoBar get _rebootGuiInfo => const InfoBar(
      title: Text("A window will pop up after the game server is started to modify its in-game settings"),
      severity: InfoBarSeverity.info
  );

  Widget get _updateError => MouseRegion(
    cursor: SystemMouseCursors.click,
    child: GestureDetector(
      onTap: _hostingController.startUpdater,
      child: const InfoBar(
          title: Text("The reboot dll couldn't be downloaded: click here to try again"),
          severity: InfoBarSeverity.info
      ),
    ),
  );
}