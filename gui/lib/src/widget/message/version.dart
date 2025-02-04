import 'dart:async';
import 'dart:io';
import 'dart:isolate';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/util/types.dart';
import 'package:reboot_launcher/src/widget/file/file_selector.dart';
import 'package:windows_taskbar/windows_taskbar.dart';

class AddVersionDialog extends StatefulWidget {
  final bool closable;
  const AddVersionDialog({Key? key, required this.closable}) : super(key: key);

  @override
  State<AddVersionDialog> createState() => _AddVersionDialogState();
}

class _AddVersionDialogState extends State<AddVersionDialog> {
  final GameController _gameController = Get.find<GameController>();
  final TextEditingController _pathController = TextEditingController();
  final GlobalKey<FormState> _formKey = GlobalKey();
  final GlobalKey<FormFieldState> _formFieldKey = GlobalKey();

  final Rx<_DownloadStatus> _status = Rx(_DownloadStatus.form);
  final Rx<_BuildSource> _source = Rx(_BuildSource.githubArchive);
  final Rxn<FortniteBuild> _build = Rxn();
  final RxnInt _timeLeft = RxnInt();
  final Rxn<double> _progress = Rxn();
  final RxInt _speed = RxInt(0);

  SendPort? _downloadPort;
  Object? _error;
  StackTrace? _stackTrace;

  @override
  void initState() {
    super.initState();
  }

  @override
  void dispose() {
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
            return Obx(() => FormDialog(
                content: _buildFormBody(downloadableBuilds),
                buttons: _formButtons
            ));
          case _DownloadStatus.downloading:
          case _DownloadStatus.extracting:
            return GenericDialog(
                header: _progressBody,
                buttons: _stopButton
            );
          case _DownloadStatus.error:
            return ErrorDialog(
                exception: _error ?? Exception(translations.unknownError),
                stackTrace: _stackTrace,
                errorMessageBuilder: (exception) {
                  var error = exception.toString();
                  error = error.after("Error: ")?.replaceAll(":", ",") ?? error.after(": ") ?? error;
                  error = error.toLowerCase();
                  return translations.downloadVersionError(error);
                }
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
      text: _source.value == _BuildSource.local ? translations.saveLocalVersion : translations.download,
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

      final source = _source.value;
      if(source == _BuildSource.local) {
        Navigator.of(context).pop();
        _addFortniteVersion(build);
        return;
      }

      _status.value = _DownloadStatus.downloading;
      final communicationPort = ReceivePort();
      communicationPort.listen((message) {
        if(message is FortniteBuildDownloadProgress) {
          _onProgress(build, message);
        }else if(message is SendPort) {
          _downloadPort = message;
        }else {
          _onDownloadError(message, null);
        }
      });
      final options = FortniteBuildDownloadOptions(
          build,
          Directory(_pathController.text),
          communicationPort.sendPort
      );
      final errorPort = ReceivePort();
      errorPort.listen((message) => _onDownloadError(message, null));
      await Isolate.spawn(
          downloadArchiveBuild,
          options,
          onError: errorPort.sendPort,
          errorsAreFatal: true
      );
    } catch (exception, stackTrace) {
      _onDownloadError(exception, stackTrace);
    }
  }

  Future<void> _onDownloadComplete(FortniteBuild build) async {
    if (!mounted) {
      return;
    }

    _status.value = _DownloadStatus.done;
    WindowsTaskbar.setProgressMode(TaskbarProgressMode.noProgress);
    _addFortniteVersion(build);
  }

  void _addFortniteVersion(FortniteBuild build) {
    WidgetsBinding.instance.addPostFrameCallback((_) => _gameController.addVersion(FortniteVersion(
        content: build.version,
        location: Directory(_pathController.text)
    )));
  }

  void _onDownloadError(Object? error, StackTrace? stackTrace) {
    _cancelDownload();
    if (!mounted) {
      return;
    }

    _status.value = _DownloadStatus.error;
    WindowsTaskbar.setProgressMode(TaskbarProgressMode.noProgress);
    _error = error;
    _stackTrace = stackTrace;
  }

  void _onProgress(FortniteBuild build, FortniteBuildDownloadProgress message) {
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

  Widget _buildFormBody(List<FortniteBuild> builds) {
    return Column(
    mainAxisSize: MainAxisSize.min,
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      _buildSourceSelector(),

      const SizedBox(
          height: 16.0
      ),

      _buildBuildSelector(builds),

      FileSelector(
          label: translations.gameFolderTitle,
          placeholder: _source.value == _BuildSource.local ? translations.gameFolderPlaceholder : translations.buildInstallationDirectoryPlaceholder,
          windowTitle: _source.value == _BuildSource.local ? translations.gameFolderPlaceWindowTitle : translations.buildInstallationDirectoryWindowTitle,
          controller: _pathController,
          validator: _source.value == _BuildSource.local ? _checkGameFolder : _checkDownloadDestination,
          folder: true,
          allowNavigator: true
      ),

      const SizedBox(
          height: 16.0
      )
    ],
  );
  }

  String? _checkGameFolder(text) {
    if (text == null || text.isEmpty) {
      return translations.emptyGamePath;
    }

    final directory = Directory(text);
    if (!directory.existsSync()) {
      return translations.directoryDoesNotExist;
    }

    if (FortniteVersionExtension.findFile(directory, "FortniteClient-Win64-Shipping.exe") == null) {
      return translations.missingShippingExe;
    }

    return null;
  }

  String? _checkDownloadDestination(text) {
    if (text == null || text.isEmpty) {
      return translations.invalidDownloadPath;
    }

    return null;
  }

  Widget _buildBuildSelector(List<FortniteBuild> builds) => InfoLabel(
      label: translations.build,
      child: FormField<FortniteBuild?>(
          key: _formFieldKey,
          validator: (data) => _checkBuild(data),
          builder: (formContext) => Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                ComboBox<FortniteBuild>(
                    placeholder: Text(translations.selectBuild),
                    isExpanded: true,
                    items: (_source.value == _BuildSource.local ? builds : builds.where((build) => build.available)).map((element) => _buildBuildItem(element)).toList(),
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

  String? _checkBuild(FortniteBuild? data) {
    if(data == null) {
      return translations.selectBuild;
    }

    final versions = _gameController.versions.value;
    if (versions.any((element) => data.version == element.content)) {
      return translations.versionAlreadyExists;
    }

    return null;
  }

  ComboBoxItem<FortniteBuild> _buildBuildItem(FortniteBuild element) => ComboBoxItem<FortniteBuild>(
      value: element,
      child: Text(element.version.toString())
  );

  Widget _buildSourceSelector() => InfoLabel(
      label: translations.source,
      child: ComboBox<_BuildSource>(
          placeholder: Text(translations.selectBuild),
          isExpanded: true,
          items: _BuildSource.values.map((entry) => _buildSourceItem(entry)).toList(),
          value: _source.value,
          onChanged: (value) {
            if(value == null){
              return;
            }

            _source.value = value;
            _updateFormDefaults();
          }
      )
  );

  ComboBoxItem<_BuildSource> _buildSourceItem(_BuildSource element) => ComboBoxItem<_BuildSource>(
      value: element,
      child: Text(element.translatedName)
  );


  List<DialogButton> get _stopButton => [
    DialogButton(
        text: translations.stopLoadingDialogAction,
        type: ButtonType.only
    )
  ];

  Future<void> _updateFormDefaults() async {
    if(_source.value != _BuildSource.local && _build.value?.available != true) {
      _build.value = null;
    }

    final disks = WindowsDisk.available();
    if(_source.value != _BuildSource.local && disks.isNotEmpty) {
      final bestDisk = disks.reduce((first, second) => first.freeBytesAvailable > second.freeBytesAvailable ? first : second);
      final build = _build.value;
      if(build == null){
        return;
      }

      final pathText = "${bestDisk.path}FortniteBuilds\\${build.version}";
      _pathController.text = pathText;
      _pathController.selection = TextSelection.collapsed(offset: pathText.length);
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

enum _BuildSource {
  local,
  githubArchive;

  String get translatedName {
    switch(this) {
      case _BuildSource.local:
        return translations.localBuild;
      case _BuildSource.githubArchive:
        return translations.githubArchive;
    }
  }
}

