import 'dart:io';
import 'package:dart_console/dart_console.dart';

typedef AutoComplete = String? Function(String);

class ConsoleParser {
  final List<Command> commands;

  ConsoleParser({required this.commands});

  CommandCall? parse(List<String> args) {
    var position = 0;
    var allowedCommands = _toMap(commands);
    var allowedParameters = <String>{};
    Command? command;
    CommandCall? head;
    CommandCall? tail;
    String? parameterName;
    while(position < args.length) {
      final current = args[position].toLowerCase();
      if(parameterName != null) {
        tail?.parameters[parameterName] = current;
        parameterName = null;
      }else if(allowedParameters.contains(current.toLowerCase())) {
        parameterName = current.substring(2);
        if(args.elementAtOrNull(position + 1) == '"') {
          position++;
        }
      }else {
        final newCommand = allowedCommands[current];
        if(newCommand != null) {
          final newCall = CommandCall(name: newCommand.name);
          if(head == null) {
            head = newCall;
            tail = newCall;
          }
          if(tail != null) {
            tail.subCall = newCall;
          }
          tail = newCall;
          command = newCommand;
          allowedCommands = _toMap(newCommand.subCommands);
          allowedParameters = _toParameters(command);
        }
      }
      position++;
    }
    return head;
  }

  Set<String> _toParameters(Command? parent) => parent?.parameters
      .map((e) => '--${e.toLowerCase()}')
      .toSet() ?? {};

  Map<String, Command> _toMap(List<Command> children) => Map.fromIterable(
      children,
      key: (command) => command.name.toLowerCase(),
      value: (command) => command
  );
}

class Command {
  final String name;
  final List<String> parameters;
  final List<Command> subCommands;

  const Command({required this.name, required this.parameters, required this.subCommands});

  @override
  String toString() => 'Command{name: $name, parameters: $parameters, subCommands: $subCommands}';
}

class Parameter {
  final String name;
  final bool Function(String) validator;

  const Parameter({required this.name, required this.validator});

  @override
  String toString() => 'Parameter{name: $name, validator: $validator}';
}

class CommandCall {
  final String name;
  final Map<String, String> parameters;
  CommandCall? subCall;

  CommandCall({required this.name}) : parameters = {};

  @override
  String toString() => 'CommandCall{name: $name, parameters: $parameters, subCall: $subCall}';
}

String runAutoComplete(AutoComplete completion) {
  final console = Console();
  console.rawMode = true;
  final position = console.cursorPosition!;

  var currentInput = '';
  var running = true;
  var result = '';

  while (running) {
    final key = console.readKey();
    switch (key.controlChar) {
      case ControlCharacter.ctrlC:
        running = false;
        break;
      case ControlCharacter.enter:
        _eraseUntil(console, position);
        console.write(currentInput);
        console.writeLine();
        result = currentInput;
        running = false;
        break;
      case ControlCharacter.tab:
        final suggestion = completion(currentInput);
        if (suggestion != null) {
          _eraseUntil(console, position);
          currentInput = suggestion;
          console.write(currentInput);
        }
        break;
      case ControlCharacter.backspace:
        if (currentInput.isNotEmpty) {
          currentInput = currentInput.substring(0, currentInput.length - 1);
          _eraseUntil(console, position);
          console.write(currentInput);
          _showSuggestion(console, position, currentInput, completion);
        }
        break;
      default:
        currentInput += key.char;
        console.write(key.char);
        _showSuggestion(console, position, currentInput, completion);
    }
  }

  return result;
}

void _eraseUntil(Console console, Coordinate position) {
  console.cursorPosition = position;
  stdout.write('\x1b[K');
}

void _showSuggestion(Console console, Coordinate position, String input, AutoComplete completion) {
  final suggestion = completion(input);
  if(suggestion == null) {
    _eraseUntil(console, position);
    console.write(input);
  }else if(suggestion.length > input.length) {
    final remaining = suggestion.substring(input.length);
    final cursorPosition = console.cursorPosition;
    console.setForegroundColor(ConsoleColor.brightBlack);
    console.write(remaining);
    console.resetColorAttributes();
    console.cursorPosition = cursorPosition;
  }
}
