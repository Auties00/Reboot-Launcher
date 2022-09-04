import 'dart:async';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/util/generic_controller.dart';
import 'package:reboot_launcher/src/widget/local_server_switch.dart';
import 'package:reboot_launcher/src/widget/port_input.dart';

import '../widget/host_input.dart';
import '../widget/server_button.dart';

class ServerPage extends StatefulWidget {
  final GenericController<bool> localController;
  final TextEditingController hostController;
  final TextEditingController portController;
  final GenericController<Process?> serverController;
  final GenericController<bool> startedServerController;

  const ServerPage(
      {Key? key,
      required this.localController,
      required this.hostController,
      required this.serverController,
      required this.portController,
      required this.startedServerController})
      : super(key: key);

  @override
  State<ServerPage> createState() => _ServerPageState();
}

class _ServerPageState extends State<ServerPage> {
  final StreamController _controller = StreamController.broadcast();

  @override
  Widget build(BuildContext context) {
    return Column(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          StreamBuilder(
              stream: _controller.stream,
              builder: (context, snapshot) => HostInput(
                  controller: widget.hostController,
                  localController: widget.localController)),
          StreamBuilder(
              stream: _controller.stream,
              builder: (context, snapshot) => PortInput(
                  controller: widget.portController,
                  localController: widget.localController)),
          LocalServerSwitch(
              controller: widget.localController,
              onSelected: (_) => _controller.add(null)),
          ServerButton(
              localController: widget.localController,
              portController: widget.portController,
              hostController: widget.hostController,
              serverController: widget.serverController,
              startController: widget.startedServerController)
        ]);
  }
}
