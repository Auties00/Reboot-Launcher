import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/ui/controller/hosting_controller.dart';
import 'package:reboot_launcher/src/ui/widget/home/launch_button.dart';
import 'package:reboot_launcher/src/ui/widget/home/version_selector.dart';
import 'package:reboot_launcher/src/ui/widget/home/setting_tile.dart';
import 'package:supabase_flutter/supabase_flutter.dart';


class BrowsePage extends StatefulWidget {
  const BrowsePage(
      {Key? key})
      : super(key: key);

  @override
  State<BrowsePage> createState() => _BrowsePageState();
}

class _BrowsePageState extends State<BrowsePage> with AutomaticKeepAliveClientMixin {
  Future? _query;
  Stream<List<Map<String, dynamic>>>? _stream;

  @override
  bool get wantKeepAlive => true;

  @override
  void initState() {
    super.initState();
    if(_query != null) {
      return;
    }

    _query = _stream != null ? Future.value(_stream) : _initStream();
  }

  Future<void> _initStream() async {
    var supabase = Supabase.instance.client;
    _stream = supabase.from('hosts')
        .stream(primaryKey: ['id'])
        .asBroadcastStream();
  }

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return FutureBuilder(
      future: _query,
      builder: (context, value) => StreamBuilder<List<Map<String, dynamic>>>(
          stream: _stream,
          builder: (context, snapshot) {
            if(snapshot.hasError){
              return Center(
                  child: Text(
                      "Cannot fetch servers: ${snapshot.error}",
                      textAlign: TextAlign.center
                  )
              );
            }

            var data = snapshot.data;
            if(data == null){
              return const SizedBox();
            }

            return Padding(
              padding: const EdgeInsets.all(12.0),
              child: Column(
                mainAxisAlignment: MainAxisAlignment.start,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                      'Server Browser',
                      textAlign: TextAlign.start,
                      style: FluentTheme.of(context).typography.title
                  ),
                  const SizedBox(
                      height: 4.0
                  ),
                  const Text(
                      'Looking for a match? This is the right place!',
                      textAlign: TextAlign.start
                  ),

                  Expanded(
                    child: Padding(
                      padding: const EdgeInsets.symmetric(vertical: 12.0),
                      child: ListView.builder(
                          itemCount: data.length,
                          itemBuilder: (context, index) {
                            var version = data[index]['version'];
                            var versionSplit = version.indexOf("-");
                            version = versionSplit != -1 ? version.substring(0, versionSplit) : version;
                            version = version.endsWith(".0") ? version.substring(0, version.length - 2) : version;
                            return SettingTile(
                              title: "${data[index]['name']} • Fortnite $version",
                              subtitle: data[index]['description'],
                              content: Button(
                                onPressed: () {},
                                child: const Text('Join'),
                              )
                          );
                          }
                      ),
                    ),
                  )
                ],
              ),
            );
          }
      )
  );
  }
}