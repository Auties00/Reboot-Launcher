
import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/dialog/implementation/server.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/setting_tile.dart';

class BrowsePage extends RebootPage {
  const BrowsePage({Key? key}) : super(key: key);

  @override
  String get name => translations.browserName;

  @override
  RebootPageType get type => RebootPageType.browser;

  @override
  String get iconAsset => "assets/images/server_browser.png";

  @override
  bool hasButton(String? pageName) => false;

  @override
  RebootPageState<BrowsePage> createState() => _BrowsePageState();
}

class _BrowsePageState extends RebootPageState<BrowsePage> {
  final HostingController _hostingController = Get.find<HostingController>();
  final MatchmakerController _matchmakerController = Get.find<MatchmakerController>();
  final TextEditingController _filterController = TextEditingController();
  final StreamController<String> _filterControllerStream = StreamController.broadcast();

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Obx(() {
      var data = _hostingController.servers.value
          ?.where((entry) => (kDebugMode || entry["id"] != _hostingController.uuid) && entry["discoverable"])
          .toSet();
      if(data == null || data.isEmpty == true) {
        return _noServers;
      }

      return _buildPageBody(data);
    });
  }

  Widget get _noServers => Column(
    mainAxisAlignment: MainAxisAlignment.center,
    crossAxisAlignment: CrossAxisAlignment.center,
    children: [
      Text(
        translations.noServersAvailableTitle,
        style: FluentTheme.of(context).typography.titleLarge,
      ),
      Text(
          translations.noServersAvailableSubtitle,
          style: FluentTheme.of(context).typography.body
      ),
    ],
  );

  Widget _buildPageBody(Set<Map<String, dynamic>> data) => StreamBuilder(
      stream: _filterControllerStream.stream,
      builder: (context, filterSnapshot)  {
        final items = data.where((entry) => _isValidItem(entry, filterSnapshot.data)).toSet();
        return Column(
          children: [
            _searchBar,
            const SizedBox(
              height: 16,
            ),
            Expanded(
              child: items.isEmpty ? _noServersByQuery : _buildPopulatedListBody(items)
            ),
          ],
        );
      }
  );

  Widget _buildPopulatedListBody(Set<Map<String, dynamic>> items) => ListView.builder(
      itemCount: items.length,
      itemBuilder: (context, index) {
        var entry = items.elementAt(index ~/ 2);
        var hasPassword = entry["password"] != null;
        return SettingTile(
            icon: Icon(
                hasPassword ? FluentIcons.lock : FluentIcons.globe
            ),
            title: Text("${_formatName(entry)} • ${entry["author"]}"),
            subtitle: Text("${_formatDescription(entry)} • ${_formatVersion(entry)}"),
            content: Button(
              onPressed: () => _matchmakerController.joinServer(_hostingController.uuid, entry),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Text(_matchmakerController.type.value == ServerType.embedded ? translations.joinServer : translations.copyIp),
                ],
              ),
            )
        );
      }
  );

  Widget get _noServersByQuery => Column(
    mainAxisAlignment: MainAxisAlignment.center,
    crossAxisAlignment: CrossAxisAlignment.center,
    children: [
      Text(
        translations.noServersAvailableByQueryTitle,
        style: FluentTheme.of(context).typography.titleLarge,
      ),
      Text(
          translations.noServersAvailableByQuerySubtitle,
          style: FluentTheme.of(context).typography.body
      ),
    ],
  );

  bool _isValidItem(Map<String, dynamic> entry, String? filter) =>
      filter == null || filter.isEmpty || _filterServer(entry, filter);

  bool _filterServer(Map<String, dynamic> element, String filter) {
    String? id = element["id"];
    if(id?.toLowerCase().contains(filter.toLowerCase()) == true) {
      return true;
    }

    var uri = Uri.tryParse(filter);
    if(uri != null
        && uri.host.isNotEmpty
        && id?.toLowerCase().contains(uri.host.toLowerCase()) == true) {
      return true;
    }

    String? name = element["name"];
    if(name?.toLowerCase().contains(filter) == true) {
      return true;
    }

    String? author = element["author"];
    if(author?.toLowerCase().contains(filter) == true) {
      return true;
    }

    String? description = element["description"];
    if(description?.toLowerCase().contains(filter) == true) {
      return true;
    }

    return false;
  }

  Widget get _searchBar => Align(
    alignment: Alignment.centerLeft,
    child: ConstrainedBox(
      constraints: BoxConstraints(
        maxWidth: 350
      ),
      child: TextBox(
        placeholder: translations.findServer,
        controller: _filterController,
        autofocus: true,
        onChanged: (value) => _filterControllerStream.add(value),
        suffix: _searchBarIcon,
      ),
    ),
  );

  Widget get _searchBarIcon => Button(
      onPressed: _filterController.text.isEmpty ? null : () {
        _filterController.clear();
        _filterControllerStream.add("");
      },
      style: ButtonStyle(
          backgroundColor: ButtonState.all(Colors.transparent),
          shape: ButtonState.all(Border())
      ),
      child: _searchBarIconData
  );

  Widget get _searchBarIconData {
    var color = FluentTheme.of(context).resources.textFillColorPrimary;
    if (_filterController.text.isNotEmpty) {
      return Icon(
          FluentIcons.clear,
          size: 8.0,
          color: color
      );
    }

    return Transform.flip(
      flipX: true,
      child: Icon(
          FluentIcons.search,
          size: 12.0,
          color: color
      ),
    );
  }

  String _formatName(Map<String, dynamic> entry) {
    String result = entry['name'];
    return result.isEmpty ? translations.defaultServerName : result;
  }

  String _formatDescription(Map<String, dynamic> entry) {
    String result = entry['description'];
    return result.isEmpty ? translations.defaultServerDescription : result;
  }

  String _formatVersion(Map<String, dynamic> entry) {
    var version = entry['version'];
    var versionSplit = version.indexOf("-");
    var minimalVersion = version = versionSplit != -1 ? version.substring(0, versionSplit) : version;
    String result = minimalVersion.endsWith(".0") ? minimalVersion.substring(0, minimalVersion.length - 2) : minimalVersion;
    if(result.toLowerCase().startsWith("fortnite ")) {
      result = result.substring(0, 10);
    }

    return "Fortnite $result";
  }

  @override
  Widget? get button => null;

  @override
  List<Widget> get settings => [];
}
