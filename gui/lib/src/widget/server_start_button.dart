import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/messenger/implementation/server.dart';
import 'package:reboot_launcher/src/util/translations.dart';

class ServerButton extends StatefulWidget {
  const ServerButton({Key? key}) : super(key: key);

  @override
  State<ServerButton> createState() => _ServerButtonState();
}

class _ServerButtonState extends State<ServerButton> {
  late final BackendController _controller = Get.find<BackendController>();
  late final StreamController<void> _textController = StreamController.broadcast();
  late final void Function() _listener = () => _textController.add(null);

  @override
  void initState() {
    _controller.port.addListener(_listener);
    super.initState();
  }

  @override
  void dispose() {
    _controller.port.removeListener(_listener);
    _textController.close();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Align(
      alignment: AlignmentDirectional.bottomCenter,
      child: SizedBox(
          height: 48,
          width: double.infinity,
          child: Button(
              child: Align(
                alignment: Alignment.center,
                child: StreamBuilder(
                    stream: _textController.stream,
                    builder: (context, snapshot) => Obx(() => Text(_buttonText))
                ),
              ),
              onPressed: () => _controller.toggleInteractive()
          )
      )
  );

  String get _buttonText {
    if(_controller.type.value == ServerType.local && _controller.port.text.trim() == kDefaultBackendPort.toString()){
      return translations.checkServer;
    }

    if(_controller.started.value){
      return translations.stopServer;
    }

    return translations.startServer;
  }
}
