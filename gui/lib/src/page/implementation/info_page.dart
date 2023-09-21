import 'dart:convert';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:intl/intl.dart';
import 'package:markdown_widget/widget/markdown.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/info_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_setting.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';
import 'package:http/http.dart' as http;
import 'package:skeletons/skeletons.dart';
import 'package:url_launcher/url_launcher.dart';

class InfoPage extends RebootPage {
  const InfoPage({Key? key}) : super(key: key);

  @override
  RebootPageState<InfoPage> createState() => _InfoPageState();

  @override
  String get name => translations.infoName;

  @override
  String get iconAsset => "assets/images/info.png";

  @override
  bool get hasButton => false;

  @override
  RebootPageType get type => RebootPageType.info;

  @override
  List<PageSetting> get settings => [];
}

class _InfoPageState extends RebootPageState<InfoPage> {
  final InfoController _infoController = Get.find<InfoController>();
  late Future<List<String>> _fetchFuture;
  late double _height;

  @override
  void initState() {
    _fetchFuture = _infoController.links != null
        ? Future.value(_infoController.links)
        : _initQuery();
    super.initState();
  }

  Future<List<String>> _initQuery() async {
    var response = await http.get(Uri.parse("https://api.github.com/repos/Auties00/reboot_launcher/contents/documentation/$currentLocale"));
    List<String> results = jsonDecode(response.body)
        .sort((first, second) {
            var firstIndex = int.parse(first["name"][0]);
            var secondIndex = int.parse(second["name"][0]);
            return firstIndex > secondIndex ? 1 : firstIndex == secondIndex ? 0 : -1;
        })
        .map<String>((entry) => entry["download_url"] as String)
        .toList();
    return _infoController.links = results;
  }

  Future<String> _readLink(String url) async {
    var known = _infoController.linksData[url];
    if(known != null) {
      return known;
    }

    var response = await http.get(Uri.parse(url));
    return _infoController.linksData[url] = response.body;
  }

  @override
  Widget build(BuildContext context) {
    super.build(context);
    _height = MediaQuery.of(context).size.height / 3;
    return FutureBuilder(
      future: _fetchFuture,
      builder: (context, linksSnapshot) {
        var linksData = linksSnapshot.data;
        return ListView.builder(
            itemBuilder: (context, index) {
              if (index % 2 == 0) {
                return const SizedBox(
                    height: 16.0
                );
              }

              return Card(
                  borderRadius: const BorderRadius.vertical(top: Radius.circular(4.0)),
                  child: _buildBody(linksData, index)
              );
            },
            itemCount: linksData == null ? null : linksData.length * 2
        );
      }
    );
  }

  Widget _buildBody(List<String>? linksData, int index) {
    if (linksData == null) {
      return SkeletonLine(
        style: SkeletonLineStyle(
            height: _height
        ),
      );
    }

    return FutureBuilder(
        future: _readLink(linksData[index ~/ 2]),
        builder: (context, linkDataSnapshot) => SizedBox(
          height: _height,
          child: MarkdownWidget(
              data: linkDataSnapshot.data ?? ""
          ),
        )
    );
  }

  @override
  List<SettingTile> get settings => [];

  @override
  Widget? get button => null;
}