
import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';

import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/matchmaker_controller.dart';
import 'package:reboot_launcher/src/interactive/server.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:skeletons/skeletons.dart';

import 'package:reboot_launcher/src/controller/hosting_controller.dart';

class BrowsePage extends StatefulWidget {
  const BrowsePage({Key? key}) : super(key: key);

  @override
  State<BrowsePage> createState() => _BrowsePageState();
}

class _BrowsePageState extends State<BrowsePage> with AutomaticKeepAliveClientMixin {
  final GameController _gameController = Get.find<GameController>();
  final MatchmakerController _matchmakerController = Get.find<MatchmakerController>();
  final TextEditingController _filterController = TextEditingController();
  final StreamController<String> _filterControllerStream = StreamController();

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return FutureBuilder(
      future: Future.delayed(const Duration(seconds: 1)), // Fake delay to show loading
      builder: (context, futureSnapshot) => Obx(() {
        var ready = futureSnapshot.connectionState == ConnectionState.done;
        var data = _gameController.servers.value;
        if(ready && data?.isEmpty == true) {
          return Column(
            mainAxisAlignment: MainAxisAlignment.center,
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              Text(
                "No servers are available right now",
                style: FluentTheme.of(context).typography.titleLarge,
              ),
              Text(
                  "Host a server yourself or come back later",
                  style: FluentTheme.of(context).typography.body
              ),
            ],
          );
        }

        return Column(
          children: [
            _buildSearchBar(ready),

            const SizedBox(
              height: 16,
            ),

            Expanded(
              child: StreamBuilder<String?>(
                  stream: _filterControllerStream.stream,
                  builder: (context, filterSnapshot)  {
                    var items = _getItems(data, filterSnapshot.data, ready);
                    var itemsCount = items != null ? items.length * 2 : null;
                    if(itemsCount == 0) {
                      return Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        crossAxisAlignment: CrossAxisAlignment.center,
                        children: [
                          Text(
                            "No results found",
                            style: FluentTheme.of(context).typography.titleLarge,
                          ),
                          Text(
                              "No server matches your query",
                              style: FluentTheme.of(context).typography.body
                          ),
                        ],
                      );
                    }

                    return ListView.builder(
                        itemCount: itemsCount,
                        itemBuilder: (context, index) {
                          if(index % 2 != 0) {
                            return const SizedBox(
                                height: 8.0
                            );
                          }

                          var entry = _getItem(index ~/ 2, items);
                          if(!ready || entry == null) {
                            return const SettingTile(
                                content: SkeletonAvatar(
                                  style: SkeletonAvatarStyle(
                                      height: 32,
                                      width: 64
                                  ),
                                )
                            );
                          }

                          var hasPassword = entry["password"] != null;
                          return SettingTile(
                              title: "${_formatName(entry)} • ${entry["author"]}",
                              subtitle: "${_formatDescription(entry)} • ${_formatVersion(entry)}",
                              content: Button(
                                onPressed: () => _matchmakerController.joinServer(entry),
                                child: Row(
                                  mainAxisAlignment: MainAxisAlignment.center,
                                  children: [
                                    if(hasPassword)
                                      const Icon(FluentIcons.lock),
                                    if(hasPassword)
                                      const SizedBox(width: 8.0),
                                    Text(_matchmakerController.type.value == ServerType.embedded ? "Join Server" : "Copy IP"),
                                  ],
                                ),
                              )
                          );
                        }
                    );
                  }
              ),
            )
          ],
        );
      }
      ),
    );
  }

  Set<Map<String, dynamic>>? _getItems(Set<Map<String, dynamic>>? data, String? filter, bool ready) {
    if (!ready) {
      return null;
    }

    if (data == null) {
      return null;
    }

    return data.where((entry) => _isValidItem(entry, filter)).toSet();
  }

  bool _isValidItem(Map<String, dynamic> entry, String? filter) =>
      (entry["discoverable"] ?? false) && (filter == null || _filterServer(entry, filter));

  bool _filterServer(Map<String, dynamic> element, String filter) {
    String? id = element["id"];
    if(id?.toLowerCase().contains(filter) == true) {
      return true;
    }

    var uri = Uri.tryParse(filter);
    if(uri != null && id?.toLowerCase().contains(uri.host.toLowerCase()) == true) {
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

  Widget _buildSearchBar(bool ready) {
    if(ready) {
      return TextBox(
        placeholder: 'Find a server',
        controller: _filterController,
        onChanged: (value) => _filterControllerStream.add(value),
        suffix: _searchBarIcon,
      );
    }

    return const SkeletonLine(
        style: SkeletonLineStyle(
            height: 32
        )
    );
  }

  Widget get _searchBarIcon => Button(
      onPressed: _filterController.text.isEmpty ? null : () {
        _filterController.clear();
        _filterControllerStream.add("");
      },
      style: ButtonStyle(
          backgroundColor: _filterController.text.isNotEmpty ? null : ButtonState.all(Colors.transparent),
          border: _filterController.text.isNotEmpty ? null : ButtonState.all(const BorderSide(color: Colors.transparent))
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

  Map<String, dynamic>? _getItem(int index, Set? data) {
    if(data == null) {
      return null;
    }

    if (index >= data.length) {
      return null;
    }

    return data.elementAt(index);
  }

  String _formatName(Map<String, dynamic> entry) {
    String result = entry['name'];
    return result.isEmpty ? kDefaultServerName : result;
  }

  String _formatDescription(Map<String, dynamic> entry) {
    String result = entry['description'];
    return result.isEmpty ? kDefaultDescription : result;
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
  bool get wantKeepAlive => true;
}
