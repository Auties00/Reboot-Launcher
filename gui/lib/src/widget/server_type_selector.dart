import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class ServerTypeSelector extends StatefulWidget {
  final bool backend;

  const ServerTypeSelector({Key? key, required this.backend})
      : super(key: key);

  @override
  State<ServerTypeSelector> createState() => _ServerTypeSelectorState();
}

class _ServerTypeSelectorState extends State<ServerTypeSelector> {
  late final ServerController _controller = widget.backend ? Get.find<BackendController>() : Get.find<MatchmakerController>();

  @override
  Widget build(BuildContext context) {
    return Obx(() => DropDownButton(
        leading: Text(_controller.type.value.label),
        items: ServerType.values
            .map((type) => _createItem(type))
            .toList()
    ));
  }

  MenuFlyoutItem _createItem(ServerType type) {
    return MenuFlyoutItem(
        text: Text(type.label),
        onPressed: () async {
          _controller.stop();
          _controller.type.value = type;
        }
    );
  }
}

extension ServerTypeExtension on ServerType {
  String get label {
    return this == ServerType.embedded ? translations.embedded
        : this == ServerType.remote ? translations.remote
        : translations.local;
  }
}
