import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/messenger/abstract/dialog.dart';
import 'package:reboot_launcher/src/messenger/abstract/overlay.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class ServerTypeSelector extends StatefulWidget {
  final Key overlayKey;
  const ServerTypeSelector({required this.overlayKey});

  @override
  State<ServerTypeSelector> createState() => _ServerTypeSelectorState();
}

class _ServerTypeSelectorState extends State<ServerTypeSelector> {
  late final BackendController _controller = Get.find<BackendController>();

  @override
  Widget build(BuildContext context) {
    return Obx(() => OverlayTarget(
      key: widget.overlayKey,
      child: DropDownButton(
          onOpen: () => inDialog = true,
          onClose: () => inDialog = false,
          leading: Text(_controller.type.value.label),
          items: ServerType.values
              .map((type) => _createItem(type))
              .toList()
      ),
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
