flutter_distributor package --platform windows --targets exe
flutter pub run msix:create
dart compile exe ./../lib/cli.dart --output ./../dist/cli/reboot.exe
