import 'dart:collection';

class Parser {
  final List<Command> commands;

  Parser({required this.commands});

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