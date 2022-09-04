// ignore_for_file: use_build_context_synchronously

import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:process_run/shell.dart';
import 'package:reboot_launcher/src/util/locate_binary.dart';
import 'package:url_launcher/url_launcher.dart';

import '../util/server.dart';
import '../util/generic_controller.dart';

class ServerButton extends StatefulWidget {
  final GenericController<bool> localController;
  final TextEditingController hostController;
  final TextEditingController portController;
  final GenericController<Process?> serverController;
  final GenericController<bool> startController;

  const ServerButton(
      {Key? key,
      required this.localController,
      required this.hostController,
      required this.portController,
      required this.serverController, required this.startController})
      : super(key: key);

  @override
  State<ServerButton> createState() => _ServerButtonState();
}

class _ServerButtonState extends State<ServerButton> {
  @override
  Widget build(BuildContext context) {
    return Align(
      alignment: AlignmentDirectional.bottomCenter,
      child: SizedBox(
        width: double.infinity,
        child: Button(
            onPressed: _onPressed,
            child: Text(widget.localController.value
                ? !widget.startController.value
                    ? "Start"
                    : "Stop"
                : "Check address")),
      ),
    );
  }

  void _onPressed() async {
    if (widget.localController.value) {
      var oldRunning = widget.startController.value;
      setState(() => widget.startController.value = !widget.startController.value); // Needed to make the UI feel smooth
      var process = await startEmbedded(context, oldRunning, true);
      var updatedRunning = process != null;
      if(updatedRunning != oldRunning){
        setState(() => widget.startController.value = updatedRunning);
      }

      widget.serverController.value = process;
      return;
    }

    checkAddress(context, widget.hostController.text, widget.portController.text);
  }
}
