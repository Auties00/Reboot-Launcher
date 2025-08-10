import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/message/backend.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:url_launcher/url_launcher.dart';

import '../messenger/info_bar.dart';

class BackendButton extends StatefulWidget {
  const BackendButton({Key? key}) : super(key: key);

  @override
  State<BackendButton> createState() => _BackendButtonState();
}

class _BackendButtonState extends State<BackendButton> {
  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final BackendController _backendController = Get.find<BackendController>();
  final StreamController<void> _textController = StreamController.broadcast();
  late final void Function() _listener = () => _textController.add(null);

  @override
  void initState() {
    _backendController.port.addListener(_listener);
    super.initState();
  }

  @override
  void dispose() {
    _backendController.port.removeListener(_listener);
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
              onPressed: () => _backendController.toggle(
                  eventHandler: (type, event) {
                    _backendController.started.value = event.type.isStart && !event.type.isError;
                    if(event.type == AuthBackendResultType.startedImplementation) {
                      _backendController.implementation = event.implementation;
                    }
                    return onBackendResult(type, event);
                  },
                  errorHandler: (error) {
                    if(_backendController.started.value) {
                      _backendController.stop();
                      _gameController.instance.value?.kill();
                      _hostingController.instance.value?.kill();
                      onBackendError(error);
                    }
                  }
              )
          )
      )
  );


  String get _buttonText {
    if(_backendController.type.value == AuthBackendType.local && _backendController.port.text.trim() == kDefaultBackendPort.toString()){
      return translations.checkServer;
    }

    if(_backendController.started.value){
      return translations.stopServer;
    }

    return translations.startServer;
  }
}
