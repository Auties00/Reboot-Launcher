import 'dart:async';
import 'dart:io';
import 'dart:isolate';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog.dart';
import 'package:reboot_launcher/src/dialog/abstract/dialog_button.dart';
import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/file_selector.dart';
import 'package:reboot_launcher/src/widget/version_name_input.dart';
import 'package:universal_disk_space/universal_disk_space.dart';
import 'package:windows_taskbar/windows_taskbar.dart';

class AddServerVersion extends StatefulWidget {
  const AddServerVersion({Key? key}) : super(key: key);

  @override
  State<AddServerVersion> createState() => _AddServerVersionState();
}

class _AddServerVersionState extends State<AddServerVersion> {
  final GameController _gameController = Get.find<GameController>();
  final BuildController _buildController = Get.find<BuildController>();
  final TextEditingController _nameController = TextEditingController();
  final TextEditingController _pathController = TextEditingController();
  final Rx<DownloadStatus> _status = Rx(DownloadStatus.form);
  final GlobalKey<FormState> _formKey = GlobalKey();
  final RxnInt _timeLeft = RxnInt();
  final Rxn<double> _progress = Rxn();

  late DiskSpace _diskSpace;
  late Future _fetchFuture;
  late Future _diskFuture;

  SendPort? _downloadPort;
  Object? _error;
  StackTrace? _stackTrace;

  @override
  void initState() {
    _fetchFuture = _buildController.builds != null
        ? Future.value(true)
        : compute(fetchBuilds, null)
        .then((value) => _buildController.builds = value);
    _diskSpace = DiskSpace();
    _diskFuture = _diskSpace.scan()
        .then((_) => _updateFormDefaults());
    super.initState();
  }

  @override
  void dispose() {
    _pathController.dispose();
    _nameController.dispose();
    _cancelDownload();
    super.dispose();
  }

  void _cancelDownload() {
    Process.run('${assetsDirectory.path}\\build\\stop.bat', []);
    _downloadPort?.send(kStopBuildDownloadSignal);
  }

  @override
  Widget build(BuildContext context) => Form(
      key: _formKey,
      child: Obx(() {
        switch(_status.value){
          case DownloadStatus.form:
            return FutureBuilder(
                future: Future.wait([_fetchFuture, _diskFuture]),
                builder: (context, snapshot) {
                  if (snapshot.hasError) {
                    WidgetsBinding.instance.addPostFrameCallback((_) => _onDownloadError(snapshot.error, snapshot.stackTrace));
                  }

                  if (!snapshot.hasData) {
                    return ProgressDialog(
                        text: translations.fetchingBuilds,
                        onStop: () => Navigator.of(context).pop()
                    );
                  }

                  return FormDialog(
                      content: _formBody,
                      buttons: _formButtons
                  );
                }
            );
          case DownloadStatus.downloading:
          case DownloadStatus.extracting:
            return GenericDialog(
                header: _progressBody,
                buttons: _stopButton
            );
          case DownloadStatus.error:
            return ErrorDialog(
                exception: _error ?? Exception(translations.unknownError),
                stackTrace: _stackTrace,
                errorMessageBuilder: (exception) => translations.downloadVersionError(exception.toString())
            );
          case DownloadStatus.done:
            return InfoDialog(
              text: translations.downloadedVersion
            );
        }
      })
  );

  List<DialogButton> get _formButtons => [
    DialogButton(type: ButtonType.secondary),
    DialogButton(
      text: translations.download,
      type: ButtonType.primary,
      onTap: () => _startDownload(context),
    )
  ];

  void _startDownload(BuildContext context) async {
    try {
      final build = _buildController.selectedBuild;
      if(build == null){
        return;
      }

      _status.value = DownloadStatus.downloading;
      final communicationPort = ReceivePort();
      communicationPort.listen((message) {
        if(message is FortniteBuildDownloadProgress) {
          _onProgress(message.progress, message.minutesLeft, message.extracting);
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

  Future<void> _onDownloadComplete() async {
    if (!mounted) {
      return;
    }

    _status.value = DownloadStatus.done;
    WindowsTaskbar.setProgressMode(TaskbarProgressMode.noProgress);
    WidgetsBinding.instance.addPostFrameCallback((_) => _gameController.addVersion(FortniteVersion(
        name: _nameController.text,
        location: Directory(_pathController.text)
    )));
  }

  void _onDownloadError(Object? error, StackTrace? stackTrace) {
    _cancelDownload();
    if (!mounted) {
      return;
    }

    _status.value = DownloadStatus.error;
    WindowsTaskbar.setProgressMode(TaskbarProgressMode.noProgress);
    _error = error;
    _stackTrace = stackTrace;
  }

  void _onProgress(double progress, int? timeLeft, bool extracting) {
    if (!mounted) {
      return;
    }

    if(progress >= 100 && extracting) {
      _onDownloadComplete();
      return;
    }

    _status.value = extracting ? DownloadStatus.extracting : DownloadStatus.downloading;
    if(progress >= 0) {
      WindowsTaskbar.setProgress(progress.round(), 100);
    }

    _timeLeft.value = timeLeft;
    _progress.value = progress;
  }

  Widget get _progressBody {
    final timeLeft = _timeLeft.value;
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Align(
          alignment: Alignment.centerLeft,
          child: Text(
            _status.value == DownloadStatus.downloading ? translations.downloading : translations.extracting,
            style: FluentTheme.maybeOf(context)?.typography.body,
            textAlign: TextAlign.start,
          ),
        ),

        const SizedBox(
          height: 8.0,
        ),

        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(
              translations.buildProgress((_progress.value ?? 0).round()),
              style: FluentTheme.maybeOf(context)?.typography.body,
            ),

            if(timeLeft != null)
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
            child: ProgressBar(value: (_progress.value ?? 0).toDouble())
        ),

        const SizedBox(
          height: 8.0,
        )
      ],
    );
  }

  Widget get _formBody => Column(
    mainAxisSize: MainAxisSize.min,
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      _buildSelectorType(),

      const SizedBox(
          height: 16.0
      ),

      _buildSelector(),

      const SizedBox(
          height: 16.0
      ),

      VersionNameInput(
          controller: _nameController
      ),

      const SizedBox(
          height: 16.0
      ),

      FileSelector(
          label: translations.buildInstallationDirectory,
          placeholder: translations.buildInstallationDirectoryPlaceholder,
          windowTitle: translations.buildInstallationDirectoryWindowTitle,
          controller: _pathController,
          validator: checkDownloadDestination,
          folder: true
      ),

      const SizedBox(
          height: 16.0
      )
    ],
  );

  Widget _buildSelectorType() => InfoLabel(
      label: translations.source,
      child: Obx(() => ComboBox<FortniteBuildSource>(
          placeholder: Text(translations.selectBuild),
          isExpanded: true,
          items: _buildSources,
          value: _buildController.selectedBuildSource,
          onChanged: (value) {
            if(value == null){
              return;
            }

            _buildController.selectedBuildSource = value;
            _updateFormDefaults();
          }
      ))
  );

  Widget _buildSelector() => InfoLabel(
      label: translations.build,
      child: Obx(() => ComboBox<FortniteBuild>(
          placeholder: Text(translations.selectBuild),
          isExpanded: true,
          items: _builds,
          value: _buildController.selectedBuild,
          onChanged: (value) {
            if(value == null){
              return;
            }

            _buildController.selectedBuild = value;
            _updateFormDefaults();
          }
      ))
  );

  List<ComboBoxItem<FortniteBuild>> get _builds => _buildController.builds!
      .where((element) => element.source == _buildController.selectedBuild?.source)
      .map((element) => _buildItem(element))
      .toList();

  ComboBoxItem<FortniteBuild> _buildItem(FortniteBuild element) => ComboBoxItem<FortniteBuild>(
      value: element,
      child: Text(element.version.toString())
  );

  List<ComboBoxItem<FortniteBuildSource>> get _buildSources => FortniteBuildSource.values
      .map((element) => _buildSourceItem(element))
      .toList();

  ComboBoxItem<FortniteBuildSource> _buildSourceItem(FortniteBuildSource element) => ComboBoxItem<FortniteBuildSource>(
      value: element,
      child: Text(_getBuildSourceName(element))
  );

  String _getBuildSourceName(FortniteBuildSource element) {
    switch(element) {
      case FortniteBuildSource.archive:
        return translations.archive;
      case FortniteBuildSource.manifest:
        return translations.manifest;
    }
  }

  List<DialogButton> get _stopButton => [
    DialogButton(
        text: "Stop",
        type: ButtonType.only
    )
  ];

  Future<void> _updateFormDefaults() async {
    if(_diskSpace.disks.isEmpty){
      return;
    }

    await _fetchFuture;
    final bestDisk = _diskSpace.disks
        .reduce((first, second) => first.availableSpace > second.availableSpace ? first : second);
    final build = _buildController.selectedBuild;
    if(build == null){
      return;
    }

    final pathText = "${bestDisk.devicePath}\\FortniteBuilds\\${build.version}";
    _pathController.text = pathText;
    _pathController.selection = TextSelection.collapsed(offset: pathText.length);
    final buildName = build.version.toString();
    _nameController.text = buildName;
    _nameController.selection = TextSelection.collapsed(offset: buildName.length);
    _formKey.currentState?.validate();
  }
}

enum DownloadStatus { form, downloading, extracting, error, done }
