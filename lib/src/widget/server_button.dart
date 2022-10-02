import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
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
                  child: Text(!_serverController.started.value
                          ? "Start"
                          : "Stop")),
            )),
      ),
    );
  }

  String get  _helpMessage {
    if (_serverController.embedded.value) {
      if (_serverController.started.value) {
        return "Stop the Lawin server currently running";
      }

      return "Start a new local Lawin server";
    }

    if (_serverController.started.value) {
      return "Stop the reverse proxy currently running";
    }

    return "Start a reverse proxy targeting the remote Lawin server";
  }

  void _onPressed(BuildContext context) async {
    var running = _serverController.started.value;
    _serverController.started.value = !running;
    if (!_serverController.embedded.value) {
      _serverController.reverseProxy = await changeReverseProxyState(
          context,
          _serverController.host.text,
          _serverController.port.text,
          _serverController.reverseProxy
      );
      _updateStarted(_serverController.reverseProxy != null);
      return;
    }

    var updatedRunning = await changeEmbeddedServerState(context, running);
    _updateStarted(updatedRunning);
  }

  void _updateStarted(bool updatedRunning) {
    if (updatedRunning == _serverController.started.value) {
      return;
    }

    _serverController.started.value = updatedRunning;
  }
}
