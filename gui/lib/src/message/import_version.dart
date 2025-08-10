import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/button/file_selector.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:version/version.dart';

class ImportVersionDialog extends StatefulWidget {
  final GameVersion? version;
  final bool closable;
  const ImportVersionDialog({Key? key, required this.version, required this.closable}) : super(key: key);

  @override
  State<ImportVersionDialog> createState() => _ImportVersionDialogState();
}

class _ImportVersionDialogState extends State<ImportVersionDialog> {
  final TextEditingController _nameController = TextEditingController();
  final GameController _gameController = Get.find<GameController>();
  final TextEditingController _pathController = TextEditingController();
  final GlobalKey<FormState> _formKey = GlobalKey();
  final Rx<_ImportState> _validator = Rx(_ImportState.inputData);

  @override
  void initState() {
    final version = widget.version;
    if(version != null) {
      _nameController.text = version.name;
      _nameController.selection = TextSelection.collapsed(offset: version.name.length);
      _pathController.text = version.location.path;
      _pathController.selection = TextSelection.collapsed(offset:  version.location.path.length);
    }

    super.initState();
  }
  @override
  void dispose() {
    _nameController.dispose();
    _pathController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Form(
      key: _formKey,
      child: Obx(() {
        switch(_validator.value) {
          case _ImportState.inputData:
            return FormDialog(
                content: _importBody,
                buttons: _importButtons
            );
          case _ImportState.validating:
            return ProgressDialog(
                text: translations.importingVersion
            );
          case _ImportState.success:
            return InfoDialog(
                text: translations.importedVersion
            );
          case _ImportState.missingShippingExeError:
            return InfoDialog(
                text: translations.importVersionMissingShippingExeError(kShippingExe)
            );
          case _ImportState.multipleShippingExesError:
            return InfoDialog(
                text: translations.importVersionMultipleShippingExesError(kShippingExe)
            );
          case _ImportState.unsupportedVersionError:
            return InfoDialog(
                text: translations.importVersionUnsupportedVersionError
            );
        }
      })
  );

  Widget get _importBody => Column(
    mainAxisSize: MainAxisSize.min,
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
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
        placeholder: translations.gameFolderPlaceholder,
        windowTitle: translations.gameFolderPlaceWindowTitle,
        controller: _pathController,
        validator: _checkGamePath,
        validatorMode: AutovalidateMode.onUserInteraction,
        folder: true,
        allowNavigator: true,
        onSelected: (selected) {
          var name = path.basename(selected);
          if(_gameController.getVersionByName(name) != null) {
            var counter = 1;
            while(_gameController.getVersionByName("$name-$counter") != null) {
              counter++;
            }
            name = "$name-$counter";
          }
          _nameController.text = name;
          _nameController.selection = TextSelection.collapsed(offset: name.length);
        },
      ),

      const SizedBox(
          height: 16.0
      )
    ],
  );

  List<DialogButton> get _importButtons => [
    if(widget.closable)
      DialogButton(type: ButtonType.secondary),
    DialogButton(
      text: translations.saveLocalVersion,
      type: widget.closable ? ButtonType.primary : ButtonType.only,
      color: FluentTheme.of(context).accentColor,
      onTap: _importVersion,
    )
  ];

  void _importVersion() async {
    final topResult = _formKey.currentState?.validate();
    if(topResult != true) {
      return;
    }

    _validator.value = _ImportState.validating;
    final name = _nameController.text.trim();
    final directory = Directory(_pathController.text.trim());

    final shippingExes = await Future.wait([
      Future.delayed(const Duration(seconds: 1)).then((_) => <File>[]),
      findFiles(directory, kShippingExe)
    ]).then((values) => values.expand((entry) => entry).toList());

    if (shippingExes.isEmpty) {
      _validator.value = _ImportState.missingShippingExeError;
      return;
    }

    if(shippingExes.length != 1) {
      _validator.value = _ImportState.multipleShippingExesError;
      return;
    }

    await patchHeadless(shippingExes.first);

    final gameVersion = await extractGameVersion(directory);
    try {
      if(Version.parse(gameVersion) >= kMaxAllowedVersion) {
        _validator.value = _ImportState.unsupportedVersionError;
        return;
      }
    }catch(_) {
      
    }

    if(widget.version == null) {
      final version = GameVersion(
          name: name,
          gameVersion: gameVersion,
          location: shippingExes.first.parent
      );
      _gameController.addVersion(version);
    }else {
      widget.version?.name = name;
      widget.version?.gameVersion = gameVersion;
      widget.version?.location = shippingExes.first.parent;
    }
    _validator.value = _ImportState.success;
  }

  String? _checkVersionName(String? text) {
    final version = widget.version;
    if(version != null && version.name == text) {
      return null;
    }

    if (text == null || text.isEmpty) {
      return translations.emptyVersionName;
    }

    if(_gameController.getVersionByName(text) != null) {
      return translations.versionAlreadyExists;
    }

    return null;
  }

  String? _checkGamePath(String? input) {
    if(input == null || input.isEmpty) {
      return translations.emptyGamePath;
    }

    final directory = Directory(input);
    if(!directory.existsSync()) {
      return translations.directoryDoesNotExist;
    }

    return null;
  }
}

enum _ImportState {
  inputData,
  validating,
  success,
  missingShippingExeError,
  multipleShippingExesError,
  unsupportedVersionError
}

