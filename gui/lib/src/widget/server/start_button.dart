import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/implementation/server.dart';

class ServerButton extends StatefulWidget {
  final bool authenticator;
  const ServerButton({Key? key, required this.authenticator}) : super(key: key);

  @override
  State<ServerButton> createState() => _ServerButtonState();
}

class _ServerButtonState extends State<ServerButton> {
  late final ServerController _controller = widget.authenticator ? Get.find<AuthenticatorController>() : Get.find<MatchmakerController>();

  @override
  Widget build(BuildContext context) => Align(
    alignment: AlignmentDirectional.bottomCenter,
    child: SizedBox(
      width: double.infinity,
      child: Obx(() => SizedBox(
        height: 48,
        child: Button(
            child: Align(
              alignment: Alignment.center,
              child: Text(_buttonText),
            ),
            onPressed: () => _controller.toggleInteractive()
        ),
      )),
    ),
  );

  String get _buttonText {
    if(_controller.type.value == ServerType.local){
      return "Check ${_controller.controllerName}";
    }

    if(_controller.started.value){
      return "Stop ${_controller.controllerName}";
    }

    return "Start ${_controller.controllerName}";
  }
}
