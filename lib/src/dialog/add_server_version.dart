import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/dialog/dialog_button.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/build.dart';
import 'package:reboot_launcher/src/widget/home/version_name_input.dart';
import 'package:universal_disk_space/universal_disk_space.dart';

import '../util/checks.dart';
import '../widget/home/build_selector.dart';
import '../widget/shared/file_selector.dart';
import 'dialog.dart';

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

  late DiskSpace _diskSpace;
  late Future _fetchFuture;
  late Future _diskFuture;

  DownloadStatus _status = DownloadStatus.form;
  String _timeLeft = "00:00:00";
  double _downloadProgress = 0;
  Process? _manifestDownloadProcess;
  CancelableOperation? _driveDownloadOperation;
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
    _buildController.addOnBuildChangedListener(() => _updateFormDefaults());
    super.initState();
  }

  @override
  void dispose() {
    _pathController.dispose();
    _nameController.dispose();
    _buildController.removeOnBuildChangedListener();
    _onDisposed();
    super.dispose();
  }

  void _onDisposed() {
    if (_status != DownloadStatus.downloading &&
        _status != DownloadStatus.extracting) {
      return;
    }

    if (_manifestDownloadProcess != null) {
      loadBinary("stop.bat", true).then(
          (value) => Process.runSync(value.path, []));
      _buildController.cancelledDownload(true);
      return;
    }

    if (_driveDownloadOperation == null) {
      return;
    }

    _driveDownloadOperation!.cancel();
    _buildController.cancelledDownload(true);
  }

  @override
  Widget build(BuildContext context) {
    switch(_status){
      case DownloadStatus.form:
        return _createFormDialog();
      case DownloadStatus.downloading:
        return GenericDialog(
            header: _createDownloadBody(),
            buttons: _createCloseButton()
        );
      case DownloadStatus.extracting:
        return GenericDialog(
            header: _createExtractingBody(),
            buttons: _createCloseButton()
        );
      case DownloadStatus.error:
        return ErrorDialog(
            exception: _error ?? Exception("unknown error"),
            stackTrace: _stackTrace,
            errorMessageBuilder: (exception) => "Cannot download version: $exception"
        );
      case DownloadStatus.done:
        return const InfoDialog(
          text: "The download was completed successfully!",
        );
    }
  }

  List<DialogButton> _createFormButtons() {
    return [
      DialogButton(type: ButtonType.secondary),
      DialogButton(
        text: "Download",
        type: ButtonType.primary,
        onTap: () => _startDownload(context),
      )
    ];
  }

  void _startDownload(BuildContext context) async {
    try {
      setState(() => _status = DownloadStatus.downloading);
      _manifestDownloadProcess = await downloadManifestBuild(
          _buildController.selectedBuild.link,
          _pathController.text,
          _onDownloadProgress
      );
      _manifestDownloadProcess!.exitCode
          .then((value) => _onDownloadComplete());
    } catch (exception, stackTrace) {
      _onDownloadError(exception, stackTrace);
    }
  }

  void _onUnrar() {
    setState(() => _status = DownloadStatus.extracting);
  }

  void _onDownloadComplete() {
    if (!mounted) {
      return;
    }

    setState(() {
      _status = DownloadStatus.done;
      _gameController.addVersion(FortniteVersion(
          name: _nameController.text,
          location: Directory(_pathController.text)
      ));
    });
  }

  void _onDownloadError(Object? error, StackTrace? stackTrace) {
    if (!mounted) {
      return;
    }

    setState(() {
      _status = DownloadStatus.error;
      _error = error;
      _stackTrace = stackTrace;
    });
  }

  void _onDownloadProgress(double progress, String timeLeft) {
    if (!mounted) {
      return;
    }

    setState(() {
      _status = DownloadStatus.downloading;
      _timeLeft = timeLeft;
      _downloadProgress = progress;
    });
  }

  Padding _createExtractingBody() {
    return Padding(
      padding: const EdgeInsets.only(bottom: 16),
      child: InfoLabel(
          label: "Extracting...",
          child: const SizedBox(width: double.infinity, child: ProgressBar())),
    );
  }

  Widget _createDownloadBody() {
    return Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Align(
                alignment: Alignment.centerLeft,
                child: Text(
                  "Downloading...",
                  style: FluentTheme.maybeOf(context)?.typography.body,
                  textAlign: TextAlign.start,
                ),
              ),
              const SizedBox(
                height: 8,
              ),

              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Text(
                    "${_downloadProgress.round()}%",
                    style: FluentTheme.maybeOf(context)?.typography.body,
                  ),

                  Text(
                    "Time left: $_timeLeft",
                    style: FluentTheme.maybeOf(context)?.typography.body,
                  )
                ],
              ),

              const SizedBox(
                height: 8,
              ),

              SizedBox(
                  width: double.infinity,
                  child: ProgressBar(value: _downloadProgress.toDouble())),
              const SizedBox(
                height: 16,
              )
            ],
          );
  }

  Widget _createFormDialog() {
    return FutureBuilder(
        future: Future.wait([_fetchFuture, _diskFuture]),
        builder: (context, snapshot) {
          if (snapshot.hasError) {
            WidgetsBinding.instance
                .addPostFrameCallback((_) =>
                _onDownloadError(snapshot.error, snapshot.stackTrace));
          }

          if (!snapshot.hasData) {
            return ProgressDialog(
                text: "Fetching builds and disks...",
                onStop: () => Navigator.of(context).pop()
            );
          }

          return FormDialog(
              content: _createFormBody(),
              buttons: _createFormButtons()
          );
        }
    );
  }

  Widget _createFormBody() {
    return Column(
      mainAxisSize: MainAxisSize.min,
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const BuildSelector(),
        const SizedBox(height: 16.0),
        VersionNameInput(controller: _nameController),
        const SizedBox(height: 16.0),
        FileSelector(
            placeholder: "Type the download destination",
            windowTitle: "Select download destination",
            controller: _pathController,
            validator: checkDownloadDestination,
            folder: true
        ),
        const SizedBox(height: 8.0),
      ],
    );
  }

  List<DialogButton> _createCloseButton() {
    return [
      DialogButton(
          text: "Stop",
          type: ButtonType.only
      )
    ];
  }

  Future<void> _updateFormDefaults() async {
    if(_diskSpace.disks.isEmpty){
      return;
    }

    await _fetchFuture;
    var bestDisk = _diskSpace.disks
        .reduce((first, second) => first.availableSpace > second.availableSpace ? first : second);
    _pathController.text = "${bestDisk.devicePath}\\FortniteBuilds\\Fortnite "
        "${_buildController.selectedBuild.version.toString()}";
    _nameController.text = _buildController.selectedBuild.version.toString();
  }
}

enum DownloadStatus { form, downloading, extracting, error, done }
