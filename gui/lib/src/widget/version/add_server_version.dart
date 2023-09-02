import 'dart:async';
import 'dart:io';
import 'dart:isolate';

import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/widget/version/version_build_selector.dart';
import 'package:reboot_launcher/src/widget/version/version_name_input.dart';
import 'package:universal_disk_space/universal_disk_space.dart';

import 'package:reboot_launcher/src/util/checks.dart';
import 'package:reboot_launcher/src/controller/build_controller.dart';
import 'package:reboot_launcher/src/widget/common/file_selector.dart';
import '../../dialog/dialog.dart';
import '../../dialog/dialog_button.dart';

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
  final Rxn<double> _downloadProgress = Rxn();

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
    Process.run('${assetsDirectory.path}\\misc\\stop.bat', []);
    _downloadPort?.send("kill");
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
                        text: "Fetching builds and disks...",
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
            return GenericDialog(
                header: _downloadBody,
                buttons: _stopButton
            );
          case DownloadStatus.extracting:
            return GenericDialog(
                header: _extractingBody,
                buttons: _stopButton
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

  List<DialogButton> get _formButtons => [
    DialogButton(type: ButtonType.secondary),
    DialogButton(
      text: "Download",
      type: ButtonType.primary,
      onTap: () => _startDownload(context),
    )
  ];

  void _startDownload(BuildContext context) async {
    try {
      var build = _buildController.selectedBuild.value;
      if(build == null){
        return;
      }

      _status.value = DownloadStatus.downloading;
      var communicationPort = ReceivePort();
      communicationPort.listen((message) {
        if(message is ArchiveDownloadProgress) {
          _onDownloadProgress(message.progress, message.minutesLeft, message.extracting);
        }else if(message is SendPort) {
          _downloadPort = message;
        }else {
          _onDownloadError("Unexpected message: $message", null);
        }
      });
      var options = ArchiveDownloadOptions(
          build.link,
          Directory(_pathController.text),
          communicationPort.sendPort
      );
      var errorPort = ReceivePort();
      errorPort.listen((message) => _onDownloadError(message, null));
      var exitPort = ReceivePort();
      exitPort.listen((message) {
        if(_status.value != DownloadStatus.error) {
          _onDownloadComplete();
        }
      });
      await Isolate.spawn(
          downloadArchiveBuild,
          options,
          onError: errorPort.sendPort,
          onExit: exitPort.sendPort,
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
    WidgetsBinding.instance.addPostFrameCallback((_) => _gameController.addVersion(FortniteVersion(
        name: _nameController.text,
        location: Directory(_pathController.text)
    )));
  }

  void _onDownloadError(Object? error, StackTrace? stackTrace) {
    if (!mounted) {
      return;
    }

    _status.value = DownloadStatus.error;
    _error = error;
    _stackTrace = stackTrace;
  }

  void _onDownloadProgress(double progress, int timeLeft, bool extracting) {
    if (!mounted) {
      return;
    }

    _status.value = extracting ? DownloadStatus.extracting : DownloadStatus.downloading;
    _timeLeft.value = timeLeft;
    _downloadProgress.value = progress;
  }

  Widget get _downloadBody {
    var timeLeft = _timeLeft.value;
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
          height: 8.0,
        ),

        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(
              "${(_downloadProgress.value ?? 0).round()}%",
              style: FluentTheme.maybeOf(context)?.typography.body,
            ),

            if(timeLeft != null)
              Text(
                "Time left: ${timeLeft == 0 ? "less than a minute" : "about $timeLeft minute${timeLeft > 1 ? 's' : ''}"}",
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
  }

  Widget get _extractingBody => Column(
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

  Widget get _formBody => Column(
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
          label: "Installation directory",
          placeholder: "Type the installation directory",
          windowTitle: "Select installation directory",
          controller: _pathController,
          validator: checkDownloadDestination,
          folder: true
      ),

      const SizedBox(
          height: 16.0
      )
    ],
  );

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
    var bestDisk = _diskSpace.disks
        .reduce((first, second) => first.availableSpace > second.availableSpace ? first : second);
    var build = _buildController.selectedBuild.value;
    if(build== null){
      return;
    }

    _pathController.text = "${bestDisk.devicePath}\\FortniteBuilds\\${build.version}";
    _nameController.text = build.version.toString();
    _formKey.currentState?.validate();
  }
}

enum DownloadStatus { form, downloading, extracting, error, done }
