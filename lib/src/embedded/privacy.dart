import 'package:jaguar/http/context/context.dart';
import 'package:reboot_launcher/src/embedded/utils.dart';

Map<String, Object?> getPrivacy(Context context) => {
  "accountId": context.pathParams.get("accountId"),
  "optOutOfPublicLeaderboards": false
};


Future<Map<String, Object?>> postPrivacy(Context context) async {
  var body = await parseBody(context);
  return {
    "accountId": context.pathParams.get("accountId"),
    "optOutOfPublicLeaderboards": body["optOutOfPublicLeaderboards"]
  };
}