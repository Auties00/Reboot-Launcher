import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/util/server.dart';

class ServerButton extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  ServerButton({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Align(
      alignment: AlignmentDirectional.bottomCenter,
      child: SizedBox(
        width: double.infinity,
        child: Obx(() => Tooltip(
          message: _helpMessage,
              child: Button(
                  onPressed: () => _onPressed(context),
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
          return "Stop the lawin server currently running";
        }

        return "Start a new local lawin server";
      case ServerType.remote:
        if (_serverController.started.value) {
          return "Stop the reverse proxy currently running";
        }

        return "Start a reverse proxy targeting the remote lawin server";
      case ServerType.local:
        return "Check if a local lawin server is running";
    }
  }

  void _onPressed(BuildContext context) async {
    var running = _serverController.started.value;
    _serverController.started.value = !running;
    switch(_serverController.type.value){
      case ServerType.embedded:
        var updatedRunning = await changeEmbeddedServerState(context, running);
        _updateStarted(updatedRunning);
        break;
      case ServerType.remote:
        _serverController.reverseProxy = await changeReverseProxyState(
            context,
            _serverController.host.text,
            _serverController.port.text,
            false,
            _serverController.reverseProxy
        );
        _updateStarted(_serverController.reverseProxy != null);
        break;
      case ServerType.local:
        var result = await checkLocalServer(
          context,
          _serverController.host.text,
          _serverController.port.text,
          false
        );
        _updateStarted(result);
        break;
    }
  }

  void _updateStarted(bool updatedRunning) {
    if (updatedRunning == _serverController.started.value) {
      return;
    }

    _serverController.started.value = updatedRunning;
  }
}
