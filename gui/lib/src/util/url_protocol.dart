import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:win32/win32.dart';

final _hive = HKEY_CURRENT_USER;

void registerUrlProtocol(String scheme, {String? executable, List<String>? arguments}) {
  final prefix = _regPrefix(scheme);
  final capitalized = scheme[0].toUpperCase() + scheme.substring(1);
  final args = _getArguments(arguments).map((a) => _sanitize(a));
  final cmd =
      '${executable ?? Platform.resolvedExecutable} ${args.join(' ')}';
  _regCreateStringKey(_hive, prefix, '', 'URL:$capitalized');
  _regCreateStringKey(_hive, prefix, 'URL Protocol', '');
  _regCreateStringKey(_hive, prefix + '\\shell\\open\\command', '', cmd);
}

void unregisterUrlProtocol(String scheme) {
  final txtKey = TEXT(_regPrefix(scheme));
  try {
    RegDeleteTree(HKEY_CURRENT_USER, txtKey);
  } finally {
    free(txtKey);
  }
}

String _regPrefix(String scheme) => 'SOFTWARE\\Classes\\$scheme';

int _regCreateStringKey(int hKey, String key, String valueName, String data) {
  final txtKey = TEXT(key);
  final txtValue = TEXT(valueName);
  final txtData = TEXT(data);
  try {
    return RegSetKeyValue(
      hKey,
      txtKey,
      txtValue,
      REG_VALUE_TYPE.REG_SZ,
      txtData,
      txtData.length * 2 + 2,
    );
  } finally {
    free(txtKey);
    free(txtValue);
    free(txtData);
  }
}

String _sanitize(String value) {
  value = value.replaceAll(r'%s', '%1').replaceAll(r'"', '\\"');
  return '"$value"';
}

List<String> _getArguments(List<String>? arguments) {
  if (arguments == null) return ['%s'];

  if (arguments.isEmpty && !arguments.any((e) => e.contains('%s'))) {
    throw ArgumentError('arguments must contain at least 1 instance of "%s"');
  }

  return arguments;
}