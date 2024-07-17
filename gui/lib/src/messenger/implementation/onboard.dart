import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/messenger/abstract/overlay.dart';
import 'package:reboot_launcher/src/messenger/implementation/profile.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/implementation/backend_page.dart';
import 'package:reboot_launcher/src/page/implementation/home_page.dart';
import 'package:reboot_launcher/src/page/implementation/host_page.dart';
import 'package:reboot_launcher/src/page/implementation/play_page.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/version_selector.dart';

void startOnboarding() {
  final settingsController = Get.find<SettingsController>();
  settingsController.firstRun.value = false;
  profileOverlayKey.currentState!.showOverlay(
      text: translations.startOnboardingText,
      offset: Offset(27.5, 17.5),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.startOnboardingActionLabel,
          onTap: () async {
            onClose();
            await showProfileForm(context);
            _promptPlayPage();
          }
      )
  );
}

void _promptPlayPage() {
  pageIndex.value = RebootPageType.play.index;
  pageOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptPlayPageText,
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptPlayPageActionLabel,
          onTap: () async {
            onClose();
            _promptPlayVersion();
          }
      )
  );
}

void _promptPlayVersion() {
  final gameController = Get.find<GameController>();
  final hasBuilds = gameController.versions.value.isNotEmpty;
  gameVersionOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptPlayVersionText,
      attachMode: AttachMode.middle,
      offset: Offset(-25, 0),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: hasBuilds ? translations.promptPlayVersionActionLabelHasBuilds : translations.promptPlayVersionActionLabelNoBuilds,
          onTap: () async {
            onClose();
            if(!hasBuilds) {
              await VersionSelector.openDownloadDialog(closable: false);
            }
            _promptServerBrowserPage();
          }
      )
  );
}

void _promptServerBrowserPage() {
  pageIndex.value = RebootPageType.browser.index;
  pageOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptServerBrowserPageText,
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptServerBrowserPageActionLabel,
          onTap: () {
            onClose();
            _promptHostPage();
          }
      )
  );
}

void _promptHostPage() {
  pageIndex.value = RebootPageType.host.index;
  pageOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptHostPageText,
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptHostPageActionLabel,
          onTap: () {
            onClose();
            _promptHostInfo();
          }
      )
  );
}


void _promptHostInfo() {
  final hostingController = Get.find<HostingController>();
  hostInfoOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptHostInfoText,
      offset: Offset(-10, 2.5),
      actionBuilder: (context, onClose) => Row(
        children: [
          _buildActionButton(
              context: context,
              label: translations.promptHostInfoActionLabelSkip,
              themed: false,
              onTap: () {
                onClose();
                hostingController.discoverable.value = false;
                _promptHostVersion();
              }
          ),
          const SizedBox(width: 12.0),
          _buildActionButton(
              context: context,
              label: translations.promptHostInfoActionLabelConfigure,
              onTap: () {
                onClose();
                hostingController.discoverable.value = true;
                hostInfoTileKey.currentState!.openNestedPage();
                WidgetsBinding.instance.addPostFrameCallback((_) => _promptHostInformation());
              }
          )
        ],
      )
  );
}

void _promptHostInformation() {
  final hostingController = Get.find<HostingController>();
  hostingController.nameFocusNode.requestFocus();
  hostInfoNameOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptHostInformationText,
      attachMode: AttachMode.middle,
      ignoreTargetPointers: false,
      offset: Offset(100, 0),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptHostInformationActionLabel,
          onTap: () {
            onClose();
            _promptHostInformationDescription();
          }
      )
  );
}

void _promptHostInformationDescription() {
  final hostingController = Get.find<HostingController>();
  hostingController.descriptionFocusNode.requestFocus();
  hostInfoDescriptionOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptHostInformationDescriptionText,
      attachMode: AttachMode.middle,
      ignoreTargetPointers: false,
      offset: Offset(70, 0),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptHostInformationDescriptionActionLabel,
          onTap: () {
            onClose();
            _promptHostInformationPassword();
          }
      )
  );
}

void _promptHostInformationPassword() {
  final hostingController = Get.find<HostingController>();
  hostingController.passwordFocusNode.requestFocus();
  hostInfoPasswordOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptHostInformationPasswordText,
      ignoreTargetPointers: false,
      attachMode: AttachMode.middle,
      offset: Offset(25, 0),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptHostInformationPasswordActionLabel,
          onTap: () {
            onClose();
            Navigator.of(hostInfoTileKey.currentContext!).pop();
            pageStack.removeLast();
            WidgetsBinding.instance.addPostFrameCallback((_) => _promptHostVersion());
          }
      )
  );
}

void _promptHostVersion() {
  hostVersionOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptHostVersionText,
      attachMode: AttachMode.end,
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptHostVersionActionLabel,
          onTap: () {
            onClose();
            _promptHostShare();
          }
      )
  );
}

void _promptHostShare() {
  final backendController = Get.find<BackendController>();
  hostShareOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptHostShareText,
      offset: Offset(-10, 2.5),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptHostShareActionLabel,
          onTap: () {
            onClose();
            backendController.type.value = ServerType.embedded;
            _promptBackendPage();
          }
      )
  );
}


void _promptBackendPage() {
  pageIndex.value = RebootPageType.backend.index;
  pageOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptBackendPageText,
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptBackendPageActionLabel,
          onTap: () {
            onClose();
            _promptBackendTypePage();
          }
      )
  );
}

void _promptBackendTypePage() {
  backendTypeOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptBackendTypePageText,
      attachMode: AttachMode.end,
      offset: Offset(-25, 0),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptBackendTypePageActionLabel,
          onTap: () {
            onClose();
            _promptBackendGameServerAddress();
          }
      )
  );
}

void _promptBackendGameServerAddress() {
  backendGameServerAddressOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptBackendGameServerAddressText,
      attachMode: AttachMode.end,
      offset: Offset(-100, 0),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptBackendGameServerAddressActionLabel,
          onTap: () {
            onClose();
            _promptBackendUnrealEngineKey();
          }
      )
  );
}

void _promptBackendUnrealEngineKey() {
  backendUnrealEngineOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptBackendUnrealEngineKeyText,
      attachMode: AttachMode.end,
      offset: Offset(-465, 2.5),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptBackendUnrealEngineKeyActionLabel,
          onTap: () {
            onClose();
            _promptBackendDetached();
          }
      )
  );
}

void _promptBackendDetached() {
  backendDetachedOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptBackendDetachedText,
      attachMode: AttachMode.end,
      offset: Offset(-410, 2.5),
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptBackendDetachedActionLabel,
          onTap: () {
            onClose();
            _promptInfoTab();
          }
      )
  );
}

void _promptInfoTab() {
  pageIndex.value = RebootPageType.info.index;
  pageOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptInfoTabText,
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptInfoTabActionLabel,
          onTap: () {
            onClose();
            _promptSettingsTab();
          }
      )
  );
}

void _promptSettingsTab() {
  pageIndex.value = RebootPageType.settings.index;
  pageOverlayTargetKey.currentState!.showOverlay(
      text: translations.promptSettingsTabText,
      actionBuilder: (context, onClose) => _buildActionButton(
          context: context,
          label: translations.promptSettingsTabActionLabel,
          onTap: onClose
      )
  );
}

Widget _buildActionButton({
  required BuildContext context,
  required String label,
  bool themed = true,
  required void Function() onTap,
}) => Button(
    style: themed ? ButtonStyle(
        backgroundColor: ButtonState.all(FluentTheme.of(context).accentColor)
    ) : null,
    child: Text(label),
    onPressed: onTap
);
