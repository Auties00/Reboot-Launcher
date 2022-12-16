import 'package:jaguar/http/context/context.dart';
import 'package:reboot_launcher/src/util/time.dart';

Map<String, Object> getVersion(Context context) =>  {
  "app": "fortnite",
  "serverDate": "2022-11-08T18:55:52.341Z",
  "overridePropertiesVersion": "unknown",
  "cln": "17951730",
  "build": "444",
  "moduleName": "Fortnite-Core",
  "buildDate": "2021-10-27T21:00:51.697Z",
  "version": "18.30",
  "branch": "Release-18.30",
  "modules": {
    "Epic-LightSwitch-AccessControlCore": {
      "cln": "17237679",
      "build": "b2130",
      "buildDate": "2021-08-19T18:56:08.144Z",
      "version": "1.0.0",
      "branch": "trunk"
    },
    "epic-xmpp-api-v1-base": {
      "cln": "5131a23c1470acbd9c94fae695ef7d899c1a41d6",
      "build": "b3595",
      "buildDate": "2019-07-30T09:11:06.587Z",
      "version": "0.0.1",
      "branch": "master"
    },
    "epic-common-core": {
      "cln": "17909521",
      "build": "3217",
      "buildDate": "2021-10-25T18:41:12.486Z",
      "version": "3.0",
      "branch": "TRUNK"
    }
  }
};

Map<String, Object> hasUpdate(Context context) => {
  "type": "NO_UPDATE"
};