import 'package:flutter/widgets.dart';

typedef MouseStateBuilderCB = Widget Function(
    BuildContext context, MouseState mouseState);

class MouseState {
  bool isMouseOver;
  bool isMouseDown;

  MouseState() : isMouseOver = false, isMouseDown = false;
}

class MouseStateBuilder extends StatefulWidget {
  final MouseStateBuilderCB builder;
  final VoidCallback? onPressed;

  const MouseStateBuilder({Key? key, required this.builder, this.onPressed})
      : super(key: key);

  @override
  State<MouseStateBuilder> createState() => _MouseStateBuilderState();
}

class _MouseStateBuilderState extends State<MouseStateBuilder> {
  late MouseState _mouseState;

  _MouseStateBuilderState() {
    _mouseState = MouseState();
  }

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
        onEnter: (event) {
          setState(() {
            _mouseState.isMouseOver = true;
          });
        },
        onExit: (event) {
          setState(() {
            _mouseState.isMouseOver = false;
          });
        },
        child: GestureDetector(
            onTapDown: (_) {
              setState(() {
                _mouseState.isMouseDown = true;
              });
            },
            onTapCancel: () {
              setState(() {
                _mouseState.isMouseDown = false;
              });
            },
            onTap: () {
              setState(() {
                _mouseState.isMouseDown = false;
                _mouseState.isMouseOver = false;
              });
              WidgetsBinding.instance.addPostFrameCallback((_) {
                if (widget.onPressed != null) {
                  widget.onPressed!();
                }
              });
            },
            onTapUp: (_) {},
            child: widget.builder(context, _mouseState)
        )
    );
  }
}
