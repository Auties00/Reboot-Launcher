import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/ui/widget/home/launch_button.dart';
import 'package:reboot_launcher/src/ui/widget/home/version_selector.dart';
import 'package:reboot_launcher/src/ui/widget/shared/setting_tile.dart';


class HostingPage extends StatefulWidget {
  const HostingPage(
      {Key? key})
      : super(key: key);

  @override
  State<HostingPage> createState() => _HostingPageState();
}

class _HostingPageState extends State<HostingPage> {
  final HostingController _hostingController = Get.find<HostingController>();

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    mainAxisSize: MainAxisSize.max,
    children: [
      const SizedBox(
        width: double.infinity,
        child: InfoBar(
            title: Text("A window will pop up after the game server is started to modify its in-game settings"),
            severity: InfoBarSeverity.info
        ),
      ),
      const SizedBox(
          height: 16.0
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
              title: "Category",
              subtitle: "The category of your game server",
              isChild: true,
              content: TextFormBox(
                  placeholder: "Category",
                  controller: _hostingController.category
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
        height: 16.0,
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
      const Expanded(child: SizedBox()),
      const LaunchButton(
        host: true
      )
    ],
  );
}