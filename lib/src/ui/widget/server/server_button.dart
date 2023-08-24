import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/ui/controller/server_controller.dart';
import 'package:reboot_launcher/src/ui/dialog/server_dialogs.dart';

class ServerButton extends StatefulWidget {
  const ServerButton({Key? key}) : super(key: key);

  @override
  State<ServerButton> createState() => _ServerButtonState();
}

class _ServerButtonState extends State<ServerButton> {
  final ServerController _serverController = Get.find<ServerController>();

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
            onPressed: () => _serverController.toggle(false)
        ),
      )),
    ),
  );

  String get _buttonText {
    if(_serverController.type.value == ServerType.local){
      return "Check backend";
    }

    if(_serverController.started.value){
      return "Stop backend";
    }

    return "Start backend";
  }
}
