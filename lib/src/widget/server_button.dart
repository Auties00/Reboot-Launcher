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
        child: Obx(() => Button(
            onPressed: () => _onPressed(context),
            child: Text(_serverController.embedded.value
                ? !_serverController.started.value
                    ? "Start"
                    : "Stop"
                : "Check address"))),
      ),
    );
  }

  void _onPressed(BuildContext context) async {
    if (!_serverController.embedded.value) {
      checkAddress(context, _serverController.host.text, _serverController.port.text);
      return;
    }

    var running = _serverController.started.value;
    _serverController.started(!running);
    var process = await startEmbedded(context, running, true);
    var updatedRunning = process != null;
    if (updatedRunning != _serverController.started.value) {
      _serverController.started.value = updatedRunning;
    }

    _serverController.process = process;
  }
}
