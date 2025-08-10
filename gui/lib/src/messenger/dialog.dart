import 'package:clipboard/clipboard.dart';
import 'package:fluent_ui/fluent_ui.dart' as fluent show showDialog;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/page/pages.dart';

import 'info_bar.dart';

bool inDialog = false;

Future<T?> showRebootDialog<T extends Object?>({required WidgetBuilder builder, bool dismissWithEsc = true}) async {
  inDialog = true;
  pagesController.add(null);
  try {
    return await fluent.showDialog(
        context: appNavigatorKey.currentContext!,
        useRootNavigator: false,
        dismissWithEsc: dismissWithEsc,
        builder: builder
    );
  }finally {
    inDialog = false;
  }
}

abstract class AbstractDialog extends StatelessWidget {
  const AbstractDialog({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context);
}

class GenericDialog extends AbstractDialog {
  final Widget header;
  final List<DialogButton> buttons;
  final EdgeInsets? padding;

  const GenericDialog({Key? key, required this.header, required this.buttons, this.padding}) : super(key: key);

  @override
  Widget build(BuildContext context) => ContentDialog(
      style: ContentDialogThemeData(
          padding: padding ?? const EdgeInsets.only(left: 20, right: 20, top: 15.0, bottom: 5.0)
      ),
      content: header,
      actions: buttons
  );
}

class FormDialog extends AbstractDialog {
  final Widget content;
  final List<DialogButton> buttons;

  const FormDialog({Key? key, required this.content, required this.buttons}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Form(
        child: Builder(
            builder: (context) {
              final parsed = buttons.map((entry) => _createFormButton(entry, context)).toList();
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
          if(!Form.of(context).validate()) {
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

  const InfoDialog({required this.text, this.buttons, Key? key}) : super(key: key);

  InfoDialog.ofOnly({required this.text, required DialogButton button, Key? key})
      : buttons = [button], super(key: key);

  @override
  Widget build(BuildContext context) {
    return GenericDialog(
        header: SizedBox(
            width: double.infinity,
            child: Text(text, textAlign: TextAlign.center)
        ),
        buttons: buttons ?? [_defaultCloseButton],
        padding: const EdgeInsets.only(left: 20, right: 20, top: 15.0, bottom: 15.0)
    );
  }

  DialogButton get _defaultCloseButton =>DialogButton(
      text: translations.defaultDialogSecondaryAction,
      type: ButtonType.only
  );
}

class ProgressDialog extends AbstractDialog {
  final String text;
  final Function()? onStop;
  final bool showButton;

  const ProgressDialog({required this.text, this.onStop, this.showButton = true, Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return GenericDialog(
        header: InfoLabel(
          label: text,
          child: Container(
              padding: const EdgeInsets.symmetric(vertical: 16.0),
              width: double.infinity,
              child: const ProgressBar()
          ),
        ),
        buttons: [
          if(showButton)
            DialogButton(
                text: translations.defaultDialogSecondaryAction,
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
  final Widget successfulBody;
  final Widget unsuccessfulBody;
  final Function(Object) errorMessageBuilder;
  final Function()? onError;
  final bool closeAutomatically;

  const FutureBuilderDialog(
      {Key? key,
        required this.future,
        required this.loadingMessage,
        required this.successfulBody,
        required this.unsuccessfulBody,
        required this.errorMessageBuilder,
        this.onError,
        this.closeAutomatically = false})  : super(key: key);

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
      return ofMessage(errorMessageBuilder(snapshot.error!));
    }

    if(snapshot.connectionState == ConnectionState.done && (snapshot.data == null || (snapshot.data is bool && !snapshot.data))){
      return unsuccessfulBody;
    }

    if (!snapshot.hasData) {
      return _createLoadingBody();
    }

    if(closeAutomatically){
      WidgetsBinding.instance
          .addPostFrameCallback((_) => Navigator.of(context).pop(true));
      return _createLoadingBody();
    }

    return successfulBody;
  }

  InfoLabel _createLoadingBody() {
    return InfoLabel(
      label: loadingMessage,
      child: Container(
          padding: const EdgeInsets.only(bottom: 16.0),
          width: double.infinity,
          child: const ProgressBar()),
    );
  }

  DialogButton _createButton(BuildContext context, AsyncSnapshot snapshot){
    return DialogButton(
        text: snapshot.hasData
            || snapshot.hasError
            || (snapshot.connectionState == ConnectionState.done && snapshot.data == null) ? translations.defaultDialogSecondaryAction : translations.stopLoadingDialogAction,
        type: ButtonType.only,
        onTap: () => Navigator.of(context).pop(!snapshot.hasError && snapshot.hasData)
    );
  }
}

class ErrorDialog extends AbstractDialog {
  final Object exception;
  final StackTrace? stackTrace;
  final Function(Object) errorMessageBuilder;

  const ErrorDialog({Key? key, required this.exception, required this.errorMessageBuilder, this.stackTrace})  : super(key: key);

  static DialogButton createCopyErrorButton({required Object error, required StackTrace? stackTrace, required Function() onClick, ButtonType type = ButtonType.primary}) => DialogButton(
    text: translations.copyErrorDialogTitle,
    type: type,
    onTap: () async {
      FlutterClipboard.controlC("$error\n$stackTrace");
      showRebootInfoBar(translations.copyErrorDialogSuccess);
      onClick();
    },
  );

  @override
  Widget build(BuildContext context) {
    return InfoDialog(
      text: errorMessageBuilder(exception),
      buttons: [
        DialogButton(
            type: stackTrace == null ? ButtonType.only : ButtonType.secondary
        ),

        if(stackTrace != null)
          createCopyErrorButton(
              error: exception,
              stackTrace: stackTrace,
              onClick: () => Navigator.pop(context)
          )
      ],
    );
  }
}

class DialogButton extends StatefulWidget {
  final String? text;
  final Function()? onTap;
  final ButtonType type;
  final Color? color;

  const DialogButton(
      {Key? key,
        this.text,
        this.onTap,
        this.color,
        required this.type})
      : assert(type != ButtonType.primary || onTap != null,
  "OnTap handler cannot be null for primary buttons"),
        assert(type != ButtonType.primary || text != null,
        "Text cannot be null for primary buttons"),
        super(key: key);

  @override
  State<DialogButton> createState() => _DialogButtonState();
}

class _DialogButtonState extends State<DialogButton> {
  @override
  Widget build(BuildContext context) => widget.type == ButtonType.only ? _onlyButton : _button;

  SizedBox get _onlyButton => SizedBox(
      width: double.infinity,
      child: _button
  );

  Widget get _button => widget.type == ButtonType.primary ? _primaryButton : _secondaryButton;

  Widget get _primaryButton => Button(
    style: ButtonStyle(
        backgroundColor: WidgetStateProperty.all(FluentTheme.of(context).accentColor)
    ),
    onPressed: widget.onTap!,
    child: Text(widget.text!),
  );

  Widget get _secondaryButton => Button(
    style: widget.color != null ? ButtonStyle(
        backgroundColor: WidgetStateProperty.all(widget.color!)
    ) : null,
    onPressed: widget.onTap ?? _onDefaultSecondaryActionTap,
    child: Text(widget.text ?? translations.defaultDialogSecondaryAction),
  );

  void _onDefaultSecondaryActionTap() => Navigator.of(context).pop(null);
}

enum ButtonType {
  primary,
  secondary,
  only
}