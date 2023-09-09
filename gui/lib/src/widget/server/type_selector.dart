import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/src/model/server_type.dart';
import 'package:reboot_launcher/src/controller/authenticator_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/controller/server_controller.dart';

class ServerTypeSelector extends StatefulWidget {
  final bool authenticator;

  const ServerTypeSelector({Key? key, required this.authenticator})
      : super(key: key);

  @override
  State<ServerTypeSelector> createState() => _ServerTypeSelectorState();
}

class _ServerTypeSelectorState extends State<ServerTypeSelector> {
  late final ServerController _controller = widget.authenticator ? Get.find<AuthenticatorController>() : Get.find<MatchmakerController>();

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
        text: Tooltip(
            message: type.message,
            child: Text(type.label)
        ),
        onPressed: () async {
          _controller.stop();
          _controller.type.value = type;
        }
    );
  }
}

extension ServerTypeExtension on ServerType {
  String get label {
    return this == ServerType.embedded ? "Embedded"
        : this == ServerType.remote ? "Remote"
        : "Local";
  }

  String get message {
    return this == ServerType.embedded ? "A server will be automatically started in the background"
        : this == ServerType.remote ? "A reverse proxy to the remote server will be created"
        : "Assumes that you are running yourself the server locally";
  }
}
