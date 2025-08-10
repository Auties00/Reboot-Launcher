import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:reboot_launcher/src/messenger/overlay.dart';

class ServerTypeSelector extends StatefulWidget {
  final Key overlayKey;
  const ServerTypeSelector({required this.overlayKey});

  @override
  State<ServerTypeSelector> createState() => _ServerTypeSelectorState();
}

class _ServerTypeSelectorState extends State<ServerTypeSelector> {
  late final BackendController _backendController = Get.find<BackendController>();

  @override
  Widget build(BuildContext context) {
    return Obx(() => OverlayTarget(
      key: widget.overlayKey,
      child: DropDownButton(
          onOpen: () => inDialog = true,
          onClose: () => inDialog = false,
          leading: Text(_backendController.type.value.label),
          items: AuthBackendType.values
              .map((type) => _createItem(type))
              .toList()
      ),
    ));
  }

  MenuFlyoutItem _createItem(AuthBackendType type) => MenuFlyoutItem(
      text: Text(type.label),
      onPressed: () async {
        await _backendController.stop();
        _backendController.type.value = type;
      }
  );
}

extension _ServerTypeExtension on AuthBackendType {
  String get label {
    return this == AuthBackendType.embedded ? translations.embedded
        : this == AuthBackendType.remote ? translations.remote
        : translations.local;
  }
}
