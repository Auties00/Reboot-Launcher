dart compile exe ./lib/watch.dart --output ./assets/browse/watch.exe
flutter_distributor package --platform windows --targets exe
flutter pub run msix:create
dart compile exe ./lib/cli.dart --output ./dist/cli/reboot.exe
