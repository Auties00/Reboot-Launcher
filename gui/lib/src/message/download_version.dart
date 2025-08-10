import 'dart:async';
import 'dart:io';
import 'dart:isolate';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/util/extensions.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/button/file_selector.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:url_launcher/url_launcher_string.dart';
import 'package:windows_taskbar/windows_taskbar.dart';

class DownloadVersionDialog extends StatefulWidget {
  final bool closable;
  const DownloadVersionDialog({Key? key, required this.closable}) : super(key: key);

  @override
  State<DownloadVersionDialog> createState() => _DownloadVersionDialogState();
}

class _DownloadVersionDialogState extends State<DownloadVersionDialog> {
  final GameController _gameController = Get.find<GameController>();
  final TextEditingController _nameController = TextEditingController();
  final TextEditingController _pathController = TextEditingController();
  final GlobalKey<FormState> _formKey = GlobalKey();
  final GlobalKey<FormFieldState> _formFieldKey = GlobalKey();

  final Rx<_DownloadStatus> _status = Rx(_DownloadStatus.form);
  final Rxn<GameBuild> _build = Rxn();
  final RxnInt _timeLeft = RxnInt();
  final Rxn<double> _progress = Rxn();
  final RxInt _speed = RxInt(0);

  SendPort? _downloadPort;
  Object? _error;

  @override
  void dispose() {
    _nameController.dispose();
    _pathController.dispose();
    _cancelDownload();
    super.dispose();
  }

  void _cancelDownload() {
    _downloadPort?.send(kStopBuildDownloadSignal);
    WindowsTaskbar.setProgressMode(TaskbarProgressMode.noProgress);
    stopDownloadServer();
  }

  @override
  Widget build(BuildContext context) => Form(
      key: _formKey,
      child: Obx(() {
        switch(_status.value){
          case _DownloadStatus.form:
            return FormDialog(
                content: _formBody,
                buttons: _formButtons
            );
          case _DownloadStatus.downloading:
          case _DownloadStatus.extracting:
            return GenericDialog(
                header: _progressBody,
                buttons: _stopButton
            );
          case _DownloadStatus.error:
            final build = _build.value;
            var error = _error?.toString() ?? translations.unknownError;
            error = error.after("Error: ")?.replaceAll(":", ",") ?? error.after(": ") ?? error;
            error = error.toLowerCase();
            return InfoDialog(
              text: translations.downloadVersionError(error),
              buttons: [
                DialogButton(
                    type: ButtonType.secondary,
                    text: translations.defaultDialogSecondaryAction
                ),
                if(build != null)
                  DialogButton(
                      type: ButtonType.primary,
                      text: translations.downloadManually,
                      onTap: () => launchUrlString(build.link)
                  ),
              ],
            );
          case _DownloadStatus.done:
            return InfoDialog(
                text: translations.downloadedVersion
            );
        }
      })
  );

  List<DialogButton> get _formButtons => [
    if(widget.closable)
      DialogButton(type: ButtonType.secondary),
    DialogButton(
      text: translations.download,
      type: widget.closable ? ButtonType.primary : ButtonType.only,
      color: FluentTheme.of(context).accentColor,
      onTap: () => _startDownload(context),
    )
  ];

  void _startDownload(BuildContext context) async {
    try {
      final topResult = _formKey.currentState?.validate();
      if(topResult != true) {
        return;
      }

      final fieldResult = _formFieldKey.currentState?.validate();
      if(fieldResult != true) {
        return;
      }

      final build = _build.value;
      if(build == null){
        return;
      }

      _status.value = _DownloadStatus.downloading;
      final communicationPort = ReceivePort();
      communicationPort.listen((message) {
        if(message is GameBuildDownloadProgress) {
          _onProgress(build, message);
        }else if(message is SendPort) {
          _downloadPort = message;
        }else {
          _onDownloadError(message);
        }
      });
      final options = GameBuildDownloadOptions(
          build,
          Directory(_pathController.text),
          communicationPort.sendPort
      );
      final errorPort = ReceivePort();
      errorPort.listen((message) => _onDownloadError(message));
      await Isolate.spawn(
          downloadArchiveBuild,
          options,
          onError: errorPort.sendPort,
          errorsAreFatal: true
      );
    } catch (exception) {
      _onDownloadError(exception);
    }
  }

  Future<void> _onDownloadComplete(GameBuild build) async {
    if (!mounted) {
      return;
    }

    final name = _nameController.text.trim();
    final location = Directory(_pathController.text);
    final files = await findFiles(location, kShippingExe);
    if(files.length == 1) {
      await patchHeadless(files.first);
    }

    _status.value = _DownloadStatus.done;
    WindowsTaskbar.setProgressMode(TaskbarProgressMode.noProgress);

    final version = GameVersion(
        name: name,
        gameVersion: build.gameVersion,
        location: location
    );
    _gameController.addVersion(version);
  }

  void _onDownloadError(Object? error) {
    _cancelDownload();
    if (!mounted) {
      return;
    }

    _status.value = _DownloadStatus.error;
    WindowsTaskbar.setProgressMode(TaskbarProgressMode.noProgress);
    _error = error;
  }

  void _onProgress(GameBuild build, GameBuildDownloadProgress message) {
    if (!mounted) {
      return;
    }

    if(message.progress >= 100 && message.extracting) {
      _onDownloadComplete(build);
      return;
    }

    _status.value = message.extracting ? _DownloadStatus.extracting : _DownloadStatus.downloading;
    if(message.progress >= 0) {
      WindowsTaskbar.setProgress(message.progress.round(), 100);
    }

    _timeLeft.value = message.timeLeft;
    _progress.value = message.progress;
    _speed.value = message.speed;
  }

  Widget get _progressBody {
    final timeLeft = _timeLeft.value;
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Align(
          alignment: Alignment.centerLeft,
          child: Text(
            _statusText,
            style: FluentTheme.maybeOf(context)?.typography.body,
            textAlign: TextAlign.start,
          ),
        ),

        if(_progress.value != null)
          const SizedBox(
            height: 8.0,
          ),

        if(_progress.value != null)
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                translations.buildProgress((_progress.value ?? 0).round()),
                style: FluentTheme.maybeOf(context)?.typography.body,
              ),

              if(timeLeft != null && timeLeft != -1)
                Text(
                  translations.timeLeft(timeLeft),
                  style: FluentTheme.maybeOf(context)?.typography.body,
                )
            ],
          ),

        const SizedBox(
          height: 8.0,
        ),

        SizedBox(
            width: double.infinity,
            child: ProgressBar(value: _progress.value?.toDouble())
        ),

        const SizedBox(
          height: 8.0,
        )
      ],
    );
  }

  String get _statusText {
    if (_status.value != _DownloadStatus.downloading) {
      return translations.extracting;
    }

    if (_progress.value == null) {
      return translations.startingDownload;
    }

    return translations.downloading;
  }

  Widget get _formBody => Column(
    mainAxisSize: MainAxisSize.min,
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      _buildSelector,

      InfoLabel(
        label: translations.versionName,
        child: TextFormBox(
            controller: _nameController,
            validator: _checkVersionName,
            placeholder: translations.versionNameLabel,
            autovalidateMode: AutovalidateMode.onUserInteraction
        ),
      ),

      const SizedBox(
          height: 16.0
      ),

      FileSelector(
          label: translations.gameFolderTitle,
          placeholder: translations.buildInstallationDirectoryPlaceholder,
          windowTitle: translations.buildInstallationDirectoryWindowTitle,
          controller: _pathController,
          validator: _checkDownloadDestination,
          folder: true,
          allowNavigator: true
      ),

      const SizedBox(
          height: 16.0
      )
    ],
  );

  String? _checkVersionName(text) {
    if (text == null || text.isEmpty) {
      return translations.emptyVersionName;
    }

    if(_gameController.getVersionByName(text) != null) {
      return translations.versionAlreadyExists;
    }

    return null;
  }

  String? _checkDownloadDestination(text) {
    if (text == null || text.isEmpty) {
      return translations.invalidDownloadPath;
    }

    return null;
  }

  Widget get _buildSelector => InfoLabel(
      label: translations.build,
      child: FormField<GameBuild?>(
          key: _formFieldKey,
          validator: (data) => _checkBuild(data),
          builder: (formContext) => Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              ComboBox<GameBuild>(
                  placeholder: Text(translations.selectBuild),
                  isExpanded: true,
                  items: downloadableBuilds.where((build) => build.available)
                      .map((element) => _buildBuildItem(element))
                      .toList(),
                  value: _build.value,
                  onChanged: (value) {
                    if(value == null){
                      return;
                    }

                    _build.value = value;
                    formContext.didChange(value);
                    formContext.validate();
                    _updateFormDefaults();
                  }
              ),
              if(formContext.hasError)
                const SizedBox(height: 4.0),
              if(formContext.hasError)
                Text(
                  formContext.errorText ?? "",
                  style: TextStyle(
                      color: Colors.red.defaultBrushFor(FluentTheme.of(context).brightness)
                  ),
                ),
              SizedBox(
                  height: formContext.hasError ? 8.0 : 16.0
              ),
            ],
          )
      )
  );

  String? _checkBuild(GameBuild? data) {
    if(data == null) {
      return translations.selectBuild;
    }

    return null;
  }

  ComboBoxItem<GameBuild> _buildBuildItem(GameBuild element) => ComboBoxItem<GameBuild>(
      value: element,
      child: Text(element.gameVersion)
  );


  List<DialogButton> get _stopButton => [
    DialogButton(
        text: translations.stopLoadingDialogAction,
        type: ButtonType.only
    )
  ];

  Future<void> _updateFormDefaults() async {
    if(_build.value?.available != true) {
      _build.value = null;
    }

    final build = _build.value;
    if(build != null) {
      _nameController.text = build.gameVersion;
      _nameController.selection = TextSelection.collapsed(offset: build.gameVersion.length);
      final disks = WindowsDisk.available();
      if(disks.isNotEmpty) {
        final bestDisk = disks.reduce((first, second) => first.freeBytesAvailable > second.freeBytesAvailable ? first : second);
        final pathText = "${bestDisk.path}FortniteBuilds\\${build.gameVersion}";
        _pathController.text = pathText;
        _pathController.selection = TextSelection.collapsed(offset: pathText.length);
      }
    }

    _formKey.currentState?.validate();
  }
}

enum _DownloadStatus {
  form,
  downloading,
  extracting,
  error,
  done
}
