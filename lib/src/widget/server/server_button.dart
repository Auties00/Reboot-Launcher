import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/dialog/server_dialogs.dart';
import 'package:reboot_launcher/src/model/server_type.dart';

class ServerButton extends StatefulWidget {
  const ServerButton({Key? key}) : super(key: key);

  @override
  State<ServerButton> createState() => _ServerButtonState();
}

class _ServerButtonState extends State<ServerButton> {
  final ServerController _serverController = Get.find<ServerController>();

  @override
  Widget build(BuildContext context) {
    return Align(
      alignment: AlignmentDirectional.bottomCenter,
      child: SizedBox(
        width: double.infinity,
        child: Obx(() => Tooltip(
          message: _helpMessage,
              child: Button(
                  onPressed: () async => _serverController.start(
                      required: false,
                      askPortKill: true
                  ),
                  child: Text(_buttonText())),
            )),
      ),
    );
  }

  String _buttonText() {
    if(_serverController.type.value == ServerType.local){
      return "Check";
    }

    if(_serverController.started.value){
      return "Stop";
    }

    return "Start";
  }

  String get _helpMessage {
    switch(_serverController.type.value){
      case ServerType.embedded:
        if (_serverController.started.value) {
          return "Stop the backend server currently running";
        }

        return "Start a new local backend server";
      case ServerType.remote:
        if (_serverController.started.value) {
          return "Stop the reverse proxy currently running";
        }

        return "Start a reverse proxy targeting the remote backend server";
      case ServerType.local:
        return "Check if a local backend server is running";
    }
  }
}
