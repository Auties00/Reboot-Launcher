import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/model/server_type.dart';

class LocalServerSwitch extends StatelessWidget {
  final ServerController _serverController = Get.find<ServerController>();

  LocalServerSwitch({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: "Determines the type of lawin server to use",
      child: InfoLabel(
        label: "Type",
        child: SizedBox(
            width: double.infinity,
            child: Obx(() => DropDownButton(
                leading: Text(_serverController.type.value.name),
                items: ServerType.values
                    .map((type) => _createItem(type))
                    .toList()))
        ),
      ),
    );
  }

  MenuFlyoutItem _createItem(ServerType type) {
    return MenuFlyoutItem(
        text: SizedBox(
            width: double.infinity,
            child: Tooltip(
                message: type.message,
                child: Text(type.name)
            )
        ),
        onPressed: () => _serverController.type(type)
    );
  }

}
