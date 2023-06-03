import 'dart:async';
import 'dart:io';

import 'package:async/async.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/model/fortnite_version.dart';
import 'package:reboot_launcher/src/util/error.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/build.dart';
import 'package:reboot_launcher/src/ui/controller/game_controller.dart';
import 'package:universal_disk_space/universal_disk_space.dart';

import '../../util/checks.dart';
import '../controller/build_controller.dart';
import '../widget/home/build_selector.dart';
import '../widget/home/version_name_input.dart';
import '../widget/shared/file_selector.dart';
import 'dialog.dart';
import 'dialog_button.dart';

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
  final Rxn<String> _timeLeft = Rxn();
  final Rxn<double> _downloadProgress = Rxn();

  late DiskSpace _diskSpace;
  late Future _fetchFuture;
  late Future _diskFuture;

  CancelableOperation? _manifestDownloadProcess;
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
    if (_status.value != DownloadStatus.extracting && _status.value != DownloadStatus.extracting) {
      return;
    }

    if (_manifestDownloadProcess == null) {
      return;
    }

    Process.run('${assetsDirectory.path}\\builds\\stop.bat', []);
    _manifestDownloadProcess?.cancel();
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
    })
  );

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
      var build = _buildController.selectedBuildRx.value;
      if(build == null){
        return;
      }

       _status.value = DownloadStatus.downloading;
      var future = downloadArchiveBuild(
          build.link,
          Directory(_pathController.text),
          (progress, eta) => _onDownloadProgress(progress, eta, false),
          (progress, eta) => _onDownloadProgress(progress, eta, true),
      );
      future.then((value) => _onDownloadComplete());
      future.onError((error, stackTrace) => _onDownloadError(error, stackTrace));
      _manifestDownloadProcess = CancelableOperation.fromFuture(future);
    } catch (exception, stackTrace) {
      _onDownloadError(exception, stackTrace);
    }
  }

  Future<void> _onDownloadComplete() async {
    if (!mounted) {
      return;
    }

    _status.value = DownloadStatus.done;
    _gameController.addVersion(FortniteVersion(
        name: _nameController.text,
        location: Directory(_pathController.text)
    ));
  }

  void _onDownloadError(Object? error, StackTrace? stackTrace) {
    if (!mounted) {
      return;
    }

    _status.value = DownloadStatus.error;
    _error = error;
    _stackTrace = stackTrace;
  }

  void _onDownloadProgress(double? progress, String? timeLeft, bool extracting) {
    if (!mounted) {
      return;
    }

    _status.value = extracting ? DownloadStatus.extracting : DownloadStatus.downloading;
    _timeLeft.value = timeLeft;
    _downloadProgress.value = progress;
  }

  Widget _createDownloadBody() => Column(
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
        height: 8.0,
      ),

      Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(
            "${(_downloadProgress.value ?? 0).round()}%",
            style: FluentTheme.maybeOf(context)?.typography.body,
          ),

          if(_timeLeft.value != null)
            Text(
              "Time left: ${_timeLeft.value}",
              style: FluentTheme.maybeOf(context)?.typography.body,
            )
        ],
      ),

      const SizedBox(
        height: 8.0,
      ),

      SizedBox(
          width: double.infinity,
          child: ProgressBar(value: (_downloadProgress.value ?? 0).toDouble())
      ),

      const SizedBox(
        height: 8.0,
      )
    ],
  );

  Widget _createExtractingBody() => Column(
    mainAxisSize: MainAxisSize.min,
    children: [
      Align(
        alignment: Alignment.centerLeft,
        child: Text(
          "Extracting...",
          style: FluentTheme.maybeOf(context)?.typography.body,
          textAlign: TextAlign.start,
        ),
      ),

      const SizedBox(
        height: 8.0,
      ),

      const SizedBox(
          width: double.infinity,
          child: ProgressBar()
      ),

      const SizedBox(
        height: 8.0,
      )
    ],
  );

  Widget _createFormBody() {
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        BuildSelector(
          onSelected: _updateFormDefaults
        ),

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
            label: "Path",
            placeholder: "Type the download destination",
            windowTitle: "Select download destination",
            controller: _pathController,
            validator: checkDownloadDestination,
            folder: true
        ),

        const SizedBox(
            height: 16.0
        )
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
    var build = _buildController.selectedBuildRx.value;
    if(build== null){
      return;
    }

    _pathController.text = "${bestDisk.devicePath}\\FortniteBuilds\\Fortnite "
        "${build.version}";
    _nameController.text = build.version.toString();
    _formKey.currentState?.validate();
  }
}

enum DownloadStatus { form, downloading, extracting, error, done }
