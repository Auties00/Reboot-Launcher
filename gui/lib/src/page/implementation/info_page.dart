import 'dart:collection';
import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/dialog/abstract/info_bar.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:path/path.dart' as path;
import 'package:reboot_launcher/src/widget/info_tile.dart';

class InfoPage extends RebootPage {
  static late List<InfoTile> _infoTiles;
  static late List<_QuizEntry> _quizEntries;

  static Object? initInfoTiles() {
    try {
      final faqDirectory = Directory("${assetsDirectory.path}\\info\\$currentLocale\\faq");
      final infoTiles = SplayTreeMap<int, InfoTile>();
      for(final entry in faqDirectory.listSync()) {
        if(entry is File) {
          final name = Uri.decodeQueryComponent(path.basename(entry.path));
          final splitter = name.indexOf(".");
          if(splitter == -1) {
            continue;
          }

          final index = int.tryParse(name.substring(0, splitter));
          if(index == null) {
            continue;
          }

          final questionName = Uri.decodeQueryComponent(name.substring(splitter + 2));
          infoTiles[index] = InfoTile(
              title: Text(questionName),
              content: Text(entry.readAsStringSync())
          );
        }
      }
      _infoTiles = infoTiles.values.toList(growable: false);

      final questionsDirectory = Directory("${assetsDirectory.path}\\info\\$currentLocale\\questions");
      final questions = SplayTreeMap<int, _QuizEntry>();
      for(final entry in questionsDirectory.listSync()) {
        if(entry is File) {
          final name = Uri.decodeQueryComponent(path.basename(entry.path));
          final splitter = name.indexOf(".");
          if(splitter == -1) {
            continue;
          }

          final index = int.tryParse(name.substring(0, splitter));
          if(index == null) {
            continue;
          }

          final questionName = Uri.decodeQueryComponent(name.substring(splitter + 2));
          questions[index] = _QuizEntry(
              question: questionName,
              options: entry.readAsStringSync().split("\n")
          );
        }
      }
      _quizEntries = questions.values.toList(growable: false);

      return null;
    }catch(error) {
      _infoTiles = [];
      _quizEntries = [];
      return error;
    }
  }

  const InfoPage({Key? key}) : super(key: key);

  @override
  RebootPageState<InfoPage> createState() => _InfoPageState();

  @override
  String get name => translations.infoName;

  @override
  String get iconAsset => "assets/images/info.png";

  @override
  bool hasButton(String? pageName) => Get.find<SettingsController>().firstRun.value && pageName != null;

  @override
  RebootPageType get type => RebootPageType.info;
}

class _InfoPageState extends RebootPageState<InfoPage> {
  final SettingsController _settingsController = Get.find<SettingsController>();
  late final Rxn<Widget> _quizPage;

  @override
  void initState() {
    _quizPage = Rxn(_settingsController.firstRun.value ? _QuizRoute(
        entries: InfoPage._quizEntries,
        onSuccess: () => _quizPage.value = null
    ) : null);
    super.initState();
  }

  @override
  List<Widget> get settings => InfoPage._infoTiles;

  @override
  Widget? get button {
    if(_quizPage.value == null) {
      return null;
    }

    return Obx(() {
      final page = _quizPage.value;
      if(page == null) {
        return const SizedBox.shrink();
      }

      return SizedBox(
          width: double.infinity,
          height: 48,
          child: Button(
            onPressed: () => Navigator.of(context).push(PageRouteBuilder(
                transitionDuration: Duration.zero,
                reverseTransitionDuration: Duration.zero,
                settings: RouteSettings(
                    name: translations.quiz
                ),
                pageBuilder: (context, incoming, outgoing) => page
            )),
            child: Text(
                translations.startQuiz
            ),
          )
      );
    });
  }
}

class _QuizRoute extends StatefulWidget {
  final List<_QuizEntry> entries;
  final void Function() onSuccess;
  const _QuizRoute({
    required this.entries,
    required this.onSuccess
  });

  @override
  State<_QuizRoute> createState() => _QuizRouteState();
}

class _QuizRouteState extends State<_QuizRoute> with AutomaticKeepAliveClientMixin {
  final SettingsController _settingsController = Get.find<SettingsController>();
  late final List<RxInt> _selectedIndexes = List.generate(widget.entries.length, (_) => RxInt(-1));
  int _triesLeft = 3;

  @override
  bool get wantKeepAlive => true;

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Column(
      children: [
        Expanded(
          child: ListView(
              children: widget.entries.indexed.expand((entry) {
                final selectedIndex = _selectedIndexes[entry.$1];
                return [
                  Text(
                    "${entry.$1 + 1}. ${entry.$2.question}",
                    style: TextStyle(
                        fontSize: 20.0,
                        fontWeight: FontWeight.w600
                    ),
                  ),
                  const SizedBox(height: 12.0),
                  ...entry.$2.options.indexed.map<Widget>((value) => Padding(
                    padding: const EdgeInsets.only(bottom: 12.0),
                    child: Obx(() => RadioButton(
                        checked: value.$1 == selectedIndex.value,
                        content: Text(value.$2, textAlign: TextAlign.center),
                        onChanged: (_) => selectedIndex.value = value.$1
                    )),
                  )),
                  const SizedBox(height: 12.0)
                ];
              }).toList()
          ),
        ),
        const SizedBox(
          height: 8.0,
        ),
        SizedBox(
          width: double.infinity,
          height: 48,
          child: Obx(() {
            var clickable = true;
            for(final index in _selectedIndexes) {
              if(index.value == -1) {
                clickable = false;
                break;
              }
            }

            return Button(
              onPressed: clickable ? () async {
                if(_triesLeft <= 0) {
                  return;
                }

                var right = 0;
                final total = widget.entries.length;
                for(var i = 0; i < total; i++) {
                  final selectedIndex = _selectedIndexes[i].value;
                  final correctIndex = widget.entries[i].correctIndex;
                  if(selectedIndex == correctIndex) {
                    right++;
                  }
                }

                if(right == total) {
                  widget.onSuccess();
                  showInfoBar(
                      translations.quizSuccess,
                      severity: InfoBarSeverity.success
                  );
                  _settingsController.firstRun.value = false;
                  Navigator.of(context).pop();
                  pageIndex.value = RebootPageType.play.index;
                  return;
                }

                switch(--_triesLeft) {
                  case 0:
                    showInfoBar(
                        translations.quizFailed(
                            right,
                            total,
                            translations.quizZeroTriesLeft
                        ),
                        severity: InfoBarSeverity.error
                    );
                    await Future.delayed(const Duration(seconds: 1));
                    exit(0);
                  case 1:
                    showInfoBar(
                        translations.quizFailed(
                            right,
                            total,
                            translations.quizOneTryLeft
                        ),
                        severity: InfoBarSeverity.error
                    );
                    break;
                  case 2:
                    showInfoBar(
                        translations.quizFailed(
                            right,
                            total,
                            translations.quizTwoTriesLeft
                        ),
                        severity: InfoBarSeverity.error
                    );
                    break;
                }
              } : null,
              child: Text(translations.checkQuiz),
            );
          },
          ),
        )
      ],
    );
  }
}

class _QuizEntry {
  final String question;
  final List<String> options;
  late final int correctIndex;

  _QuizEntry({required this.question, required this.options}) {
    final correct = options.first;
    options.shuffle();
    correctIndex = options.indexOf(correct);
  }
}