import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/model/server_type.dart';
import 'package:reboot_launcher/src/ui/controller/server_controller.dart';

class ServerTypeSelector extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  ServerTypeSelector({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return DropDownButton(
        leading: Text(_serverController.type.value.name),
        items: ServerType.values
            .map((type) => _createItem(type))
            .toList()
    );
  }

  MenuFlyoutItem _createItem(ServerType type) {
    return MenuFlyoutItem(
        text: Tooltip(
            message: type.message,
            child: Text(type.name)
        ),
        onPressed: () async {
          await _serverController.stop();
          _serverController.type(type);
        }
    );
  }

}
