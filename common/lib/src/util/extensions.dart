import 'dart:convert';
import 'dart:io';

extension ProcessExtension on Process {
  Stream<String> get stdOutput => this.stdout.expand((event) => utf8.decode(event, allowMalformed: true).split("\n"));

  Stream<String> get stdError => this.stderr.expand((event) => utf8.decode(event, allowMalformed: true).split("\n"));
}

extension StringExtension on String {
  bool get isBlankOrEmpty {
    if(isEmpty) {
      return true;
    }

    for(var char in this.split("")) {
      if(char != " ") {
        return false;
      }
    }

    return true;
  }
}