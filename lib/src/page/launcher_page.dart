import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/widget/deployment_selector.dart';
import 'package:reboot_launcher/src/widget/launch_button.dart';
import 'package:reboot_launcher/src/widget/username_box.dart';
import 'package:get/get.dart';

import 'package:reboot_launcher/src/widget/version_selector.dart';
import 'package:url_launcher/url_launcher.dart';

import '../widget/warning_info.dart';

class LauncherPage extends StatefulWidget {
  final bool ready;
  final Object? error;
  final StackTrace? stackTrace;

  const LauncherPage(
      {Key? key, required this.ready, required this.error, this.stackTrace})
      : super(key: key);

  @override
  State<LauncherPage> createState() => _LauncherPageState();
}

class _LauncherPageState extends State<LauncherPage> {
  final BuildController _buildController = Get.find<BuildController>();
  bool shouldWriteError = true;

  @override
  void initState() {
    _buildController.cancelledDownload
        .listen((value) => value ? _onCancelWarning() : {});
    super.initState();
  }

  void _onCancelWarning() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      showSnackbar(context,
          const Snackbar(content: Text("Download cancelled")));
      _buildController.cancelledDownload.value = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    if (!widget.ready && widget.error == null) {
      return Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: const [
              ProgressRing(),
              SizedBox(height: 16.0),
              Text("Updating Reboot DLL...")
            ],
          ),
        ],
      );
    }

    return Column(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if(widget.error != null)
          WarningInfo(
            text: "Cannot update Reboot DLL",
            icon: FluentIcons.info,
            severity: InfoBarSeverity.warning,
            onPressed: () async {
              if (shouldWriteError) {
                await errorFile.writeAsString(
                    "Error: ${widget.error}\nStacktrace: ${widget.stackTrace}",
                    mode: FileMode.write
                );
                shouldWriteError = false;
              }

              launchUrl(errorFile.uri);
            },
          ),
        UsernameBox(),
        VersionSelector(),
        DeploymentSelector(enabled: true),
        const LaunchButton()
      ],
    );
  }
}
