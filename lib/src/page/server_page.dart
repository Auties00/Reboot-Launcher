import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_launcher/src/widget/local_server_switch.dart';
import 'package:reboot_launcher/src/widget/port_input.dart';

import 'package:reboot_launcher/src/widget/host_input.dart';
import 'package:reboot_launcher/src/widget/server_button.dart';

import '../controller/server_controller.dart';

class ServerPage extends StatefulWidget {
  const ServerPage({Key? key}) : super(key: key);

  @override
  State<ServerPage> createState() => _ServerPageState();
}

class _ServerPageState extends State<ServerPage> {
  final ServerController _serverController = Get.find<ServerController>();

  @override
  void initState() {
    if (_serverController.warning.value) {
      WidgetsBinding.instance.addPostFrameCallback((_) async {
        await _showAdvancedUserWarning();
        _serverController.warning.value = false;
      });
    }

    super.initState();
  }

  Future<void> _showAdvancedUserWarning() async {
    await showDialog(
        context: context,
        builder: (context) => ContentDialog(
          content: const SizedBox(
            width: double.infinity,
            child: Text("This section is reserved for advanced users",
                textAlign: TextAlign.center),
          ),
          actions: [
            SizedBox(
              width: double.infinity,
              child: FilledButton(
                onPressed: () => Navigator.of(context).pop(),
                child: const Text('I understand'),
              ),
            )
          ],
        )
    );
  }

  @override
  Widget build(BuildContext context) {
    return Column(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          HostInput(),
          PortInput(),
          LocalServerSwitch(),
          ServerButton()
        ]);
  }
}
