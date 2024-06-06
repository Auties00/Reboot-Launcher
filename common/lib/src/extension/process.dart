import 'dart:convert';
import 'dart:io';

extension ProcessExtension on Process {
  Stream<String> get stdOutput => this.stdout.expand((event) => utf8.decode(event, allowMalformed: true).split("\n"));

  Stream<String> get stdError => this.stderr.expand((event) => utf8.decode(event, allowMalformed: true).split("\n"));
}