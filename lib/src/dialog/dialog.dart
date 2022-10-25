import 'package:clipboard/clipboard.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/material.dart';
import 'package:reboot_launcher/src/dialog/snackbar.dart';

import 'dialog_button.dart';

abstract class AbstractDialog extends StatelessWidget {
  const AbstractDialog({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context);
}

class GenericDialog extends AbstractDialog {
  final Widget header;
  final List<DialogButton> buttons;
  final EdgeInsets? padding;

  const GenericDialog({super.key, required this.header, required this.buttons, this.padding});

  @override
  Widget build(BuildContext context) {
    return ContentDialog(
        style:  ContentDialogThemeData(
            padding: padding ?? const EdgeInsets.only(left: 20, right: 20, top: 15.0, bottom: 5.0)
        ),
        content: header,
        actions: buttons
    );
  }
}

class FormDialog extends AbstractDialog {
  final Widget content;
  final List<DialogButton> buttons;

  const FormDialog({super.key, required this.content, required this.buttons});

  @override
  Widget build(BuildContext context) {
    return Form(
        child: Builder(
            builder: (context) {
              var parsed = buttons.map((entry) => _createFormButton(entry, context)).toList();
              return GenericDialog(
                  header: content,
                  buttons: parsed
              );
            }
        )
    );
  }

  DialogButton _createFormButton(DialogButton entry, BuildContext context) {
    if (entry.type != ButtonType.primary) {
      return entry;
    }

    return DialogButton(
        text: entry.text,
        type: entry.type,
        onTap: () {
          if(!Form.of(context)!.validate()) {
            return;
          }

          entry.onTap?.call();
        }
    );
  }
}

class InfoDialog extends AbstractDialog {
  final String text;
  final List<DialogButton>? buttons;

  const InfoDialog({required this.text, this.buttons, super.key});

  InfoDialog.ofOnly({required this.text, required DialogButton button, super.key})
      : buttons = [button];

  @override
  Widget build(BuildContext context) {
    return GenericDialog(
        header: SizedBox(
            width: double.infinity,
            child: Text(text, textAlign: TextAlign.center)
        ),
        buttons: buttons ?? [_createDefaultButton()],
        padding: const EdgeInsets.only(left: 20, right: 20, top: 15.0, bottom: 15.0)
    );
  }

  DialogButton _createDefaultButton() {
    return DialogButton(
            text: "Close",
            type: ButtonType.only
        );
  }
}

class ProgressDialog extends AbstractDialog {
  final String text;
  final Function()? onStop;

  const ProgressDialog({required this.text, this.onStop, super.key});

  @override
  Widget build(BuildContext context) {
    return GenericDialog(
        header: InfoLabel(
          label: text,
          child: Container(
              padding: const EdgeInsets.only(bottom: 16.0),
              width: double.infinity,
              child: const ProgressBar()
          ),
        ),
        buttons: [
          DialogButton(
            text: "Close",
            type: ButtonType.only,
            onTap: onStop
          )
        ]
    );
  }
}

class FutureBuilderDialog extends AbstractDialog {
  final Future future;
  final String loadingMessage;
  final Widget loadedBody;
  final Function(Object) errorMessageBuilder;
  final Function()? onError;
  final bool closeAutomatically;

  const FutureBuilderDialog(
      {super.key,
      required this.future,
      required this.loadingMessage,
      required this.loadedBody,
      required this.errorMessageBuilder,
      this.onError,
      this.closeAutomatically = false});

  static Container ofMessage(String message) {
    return Container(
        width: double.infinity,
        padding: const EdgeInsets.only(bottom: 16.0),
        child: Text(
            message,
            textAlign: TextAlign.center
        )
    );
  }

  @override
  Widget build(BuildContext context) {
    return FutureBuilder(
        future: future,
        builder: (context, snapshot) => GenericDialog(
            header: _createBody(context, snapshot),
            buttons: [_createButton(context, snapshot)]
        )
    );
  }

  Widget _createBody(BuildContext context, AsyncSnapshot snapshot){
    if (snapshot.hasError) {
      onError?.call();
      return ofMessage(snapshot.error.toString());
    }

    if (!snapshot.hasData) {
      return InfoLabel(
        label: loadingMessage,
        child: Container(
            padding: const EdgeInsets.only(bottom: 16.0),
            width: double.infinity,
            child: const ProgressBar()),
      );
    }

    if(closeAutomatically){
      Navigator.of(context).pop(true);
    }

    return loadedBody;
  }

  DialogButton _createButton(BuildContext context, AsyncSnapshot snapshot){
    return DialogButton(
      text: snapshot.hasData || snapshot.hasError ? "Close" : "Stop",
      type: ButtonType.only,
      onTap: () => Navigator.of(context).pop(!snapshot.hasError && snapshot.hasData)
    );
  }
}

class ErrorDialog extends AbstractDialog {
  final Object exception;
  final StackTrace? stackTrace;
  final Function(Object) errorMessageBuilder;

  const ErrorDialog({super.key, required this.exception, required this.errorMessageBuilder, this.stackTrace});

  @override
  Widget build(BuildContext context) {
    return InfoDialog(
      text: errorMessageBuilder(exception),
      buttons: [
        DialogButton(
            type: stackTrace == null ? ButtonType.only : ButtonType.secondary
        ),

        if(stackTrace != null)
          DialogButton(
            text: "Copy error",
            type: ButtonType.primary,
            onTap: () async {
              FlutterClipboard.controlC("An error occurred: $exception\nStacktrace:\n $stackTrace.toString");
              Navigator.of(context).pop();
              showMessage("Copied error to clipboard");
            },
          )
      ],
    );
  }
}