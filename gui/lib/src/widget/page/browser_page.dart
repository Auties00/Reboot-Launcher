import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:fluentui_system_icons/fluentui_system_icons.dart' as fluentUiIcons;
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/backend_controller.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/messenger/dialog.dart';
import 'package:reboot_launcher/src/page/page.dart';
import 'package:reboot_launcher/src/page/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/fluent/setting_tile.dart';

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
  final GameController _gameController = Get.find<GameController>();
  final HostingController _hostingController = Get.find<HostingController>();
  final BackendController _backendController = Get.find<BackendController>();
  final TextEditingController _filterController = TextEditingController();
  final StreamController<String> _filterControllerStream = StreamController.broadcast();

  final Rx<_Filter> _filter = Rx(_Filter.all);
  final Rx<_Sort> _sort = Rx(_Sort.timeDescending);

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Obx(() {
      final data = _hostingController.servers.value
          ?.where((entry) => (kDebugMode || entry.id != _hostingController.uuid) && entry.discoverable)
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

  Widget _buildPageBody(Set<FortniteServer> data) => StreamBuilder(
      stream: _filterControllerStream.stream,
      builder: (context, filterSnapshot)  {
        final items = data.where((entry) => _isValidItem(entry, filterSnapshot.data)).toSet();
        return Column(
          children: [
            _searchBar,
            const SizedBox(
              height: 24,
            ),
            Row(
              children: [
                _buildFilter(context),
                const SizedBox(
                    width: 16.0
                ),
                _buildSort(context),
              ],
            ),
            const SizedBox(
              height: 24,
            ),
            Expanded(
                child: _buildPopulatedListBody(items)
            ),
          ],
        );
      }
  );

  Widget _buildSort(BuildContext context) => Row(
    children: [
      Icon(
          fluentUiIcons.FluentIcons.arrow_sort_24_regular,
          color: FluentTheme.of(context).resources.textFillColorDisabled
      ),
      const SizedBox(width: 4.0),
      Text(
        "Sort by: ",
        style: TextStyle(
            color: FluentTheme.of(context).resources.textFillColorDisabled
        ),
      ),
      const SizedBox(width: 4.0),
      Obx(() => SizedBox(
        width: 230,
        child: DropDownButton(
            onOpen: () => inDialog = true,
            onClose: () => inDialog = false,
            leading: Text(
                _sort.value.translatedName,
                textAlign: TextAlign.start
            ),
            title: const Spacer(),
            items: _Sort.values.map((entry) => MenuFlyoutItem(
                text: Text(entry.translatedName),
                onPressed: () => _sort.value = entry
            )).toList()
        ),
      ))
    ],
  );

  Row _buildFilter(BuildContext context) {
    return Row(
      children: [
        Icon(
            fluentUiIcons.FluentIcons.filter_24_regular,
            color: FluentTheme.of(context).resources.textFillColorDisabled
        ),
        const SizedBox(width: 4.0),
        Text(
          "Filter by: ",
          style: TextStyle(
              color: FluentTheme.of(context).resources.textFillColorDisabled
          ),
        ),
        const SizedBox(width: 4.0),
        Obx(() => SizedBox(
          width: 125,
          child: DropDownButton(
              onOpen: () => inDialog = true,
              onClose: () => inDialog = false,
              leading: Text(
                  _filter.value.translatedName,
                  textAlign: TextAlign.start
              ),
              title: const Spacer(),
              items: _Filter.values.map((entry) => MenuFlyoutItem(
                  text: Text(entry.translatedName),
                  onPressed: () => _filter.value = entry
              )).toList()
          ),
        ))
      ],
    );
  }

  Widget _buildPopulatedListBody(Set<FortniteServer> items) => Obx(() {
    final filter = _filter.value;
    final sorted = items.where((element) {
      switch(filter) {
        case _Filter.all:
          return true;
        case _Filter.accessible:
          return element.password == null;
        case _Filter.playable:
          return _gameController.getVersionByGame(element.version) != null;
      }
    }).toList();
    final sort = _sort.value;
    sorted.sort((first, second) {
      switch(sort) {
        case _Sort.timeAscending:
          return first.timestamp.compareTo(second.timestamp);
        case _Sort.timeDescending:
          return second.timestamp.compareTo(first.timestamp);
        case _Sort.nameAscending:
          return first.name.compareTo(second.name);
        case _Sort.nameDescending:
          return second.name.compareTo(first.name);
      }
    });
    if(sorted.isEmpty) {
      return _noServersByQuery;
    }

    return ListView.builder(
        itemCount: sorted.length,
        itemBuilder: (context, index) {
          final entry = sorted.elementAt(index);
          final hasPassword = entry.password != null;
          return SettingTile(
              icon: Icon(
                  hasPassword ? FluentIcons.lock : FluentIcons.globe
              ),
              title: Text(
                  "${_formatName(entry)} • ${entry.author}",
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis
              ),
              subtitle: Text(
                  "${_formatDescription(entry)} • ${_formatVersion(entry)}",
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis
              ),
              content: Button(
                onPressed: () => _backendController.joinServer(_hostingController.uuid, entry),
                child: Text(_backendController.type.value == ServerType.embedded ? translations.joinServer : translations.copyIp),
              )
          );
        }
    );
  });

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

  bool _isValidItem(FortniteServer entry, String? filter) =>
      filter == null || filter.isEmpty || _filterServer(entry, filter);

  bool _filterServer(FortniteServer element, String filter) {
    filter = filter.toLowerCase();

    final uri = Uri.tryParse(filter);
    if(uri != null && uri.host.isNotEmpty && element.id.toLowerCase().contains(uri.host.toLowerCase())) {
      return true;
    }

    return element.id.toLowerCase().contains(filter.toLowerCase())
        || element.name.toLowerCase().contains(filter)
        || element.author.toLowerCase().contains(filter)
        || element.description.toLowerCase().contains(filter);
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
          backgroundColor: WidgetStateProperty.all(Colors.transparent),
          shape: WidgetStateProperty.all(Border())
      ),
      child: _searchBarIconData
  );

  Widget get _searchBarIconData {
    final color = FluentTheme.of(context).resources.textFillColorPrimary;
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

  String _formatName(FortniteServer server) {
    final result = server.name;
    return result.isEmpty ? translations.defaultServerName : result;
  }

  String _formatDescription(FortniteServer server) {
    final result = server.description;
    return result.isEmpty ? translations.defaultServerDescription : result;
  }

  String _formatVersion(FortniteServer server) => "Fortnite ${server.version.toString()}";

  @override
  Widget? get button => null;

  @override
  List<Widget> get settings => [];
}

enum _Filter {
  all,
  accessible,
  playable;

  String get translatedName {
    switch(this) {
      case _Filter.all:
        return translations.all;
      case _Filter.accessible:
        return translations.accessible;
      case _Filter.playable:
        return translations.playable;
    }
  }
}

enum _Sort {
  timeAscending,
  timeDescending,
  nameAscending,
  nameDescending;

  String get translatedName {
    switch(this) {
      case _Sort.timeAscending:
        return translations.timeAscending;
      case _Sort.timeDescending:
        return translations.timeDescending;
      case _Sort.nameAscending:
        return translations.nameAscending;
      case _Sort.nameDescending:
        return translations.nameDescending;
    }
  }
}