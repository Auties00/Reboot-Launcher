import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/rendering.dart';
import 'package:reboot_launcher/src/page/pages.dart';

typedef WidgetBuilder = Widget Function(BuildContext, void Function());

class OverlayTarget extends StatefulWidget {
  final Widget child;
  const OverlayTarget({super.key, required this.child});

  @override
  State<OverlayTarget> createState() => OverlayTargetState();

  OverlayTargetState of(BuildContext context) => context.findAncestorStateOfType<OverlayTargetState>()!;
}

class OverlayTargetState extends State<OverlayTarget> {
  @override
  Widget build(BuildContext context) => widget.child;

  void showOverlay({
    required String text,
    required WidgetBuilder actionBuilder,
    Offset offset = Offset.zero,
    bool ignoreTargetPointers = true,
    AttachMode attachMode = AttachMode.start
  }) {
    final renderBox = context.findRenderObject() as RenderBox;
    final position = renderBox.localToGlobal(Offset.zero);
    final color = FluentTheme.of(context).acrylicBackgroundColor;
    late OverlayEntry entry;
    entry = OverlayEntry(
        builder: (context) => Stack(
          children: [
            Container(
              width: double.infinity,
              height: double.infinity,
              child: _AbsorbPointer(
                exclusion: ignoreTargetPointers ? null : renderBox
              )
            ),
            Positioned(
              left: position.dx - (attachMode != AttachMode.start ? renderBox.size.width : 0) + offset.dx,
              top: position.dy + (renderBox.size.height / 2) + offset.dy,
              child: CustomPaint(
                painter: _CallOutShape(color, attachMode != AttachMode.start),
                child: Padding(
                  padding: const EdgeInsets.all(12.0),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(text),
                      const SizedBox(height: 12.0),
                      actionBuilder(context, () => entry.remove())
                    ],
                  ),
                ),
              ),
            ),
          ],
        )
    );
    appOverlayKey.currentState?.insert(entry);
  }
}

enum AttachMode {
  start,
  middle,
  end;
}

// Harder than one would think
class _CallOutShape extends CustomPainter {
  final Color color;
  final bool end;
  _CallOutShape(this.color, this.end);

  @override
  void paint(Canvas canvas, Size size) {
    final fillPaint = Paint()
      ..color = color
      ..style = PaintingStyle.fill;

    final borderPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 0.25
      ..color = Colors.white;

    final path = Path();
    path.moveTo(10, 0);
    if(!end) {
      path.lineTo(12.5, 0);
      path.lineTo(20, -12.5);
      path.lineTo(27.5, 0);
    }else {
      path.lineTo(size.width - 27.5, 0);
      path.lineTo(size.width - 20, -12.5);
      path.lineTo(size.width - 12.5, 0);
    }

    path.lineTo(size.width - 10, 0);
    path.arcToPoint(Offset(size.width, 10), radius: Radius.circular(10));
    path.lineTo(size.width, size.height - 10);
    path.arcToPoint(Offset(size.width - 10, size.height), radius: Radius.circular(10));
    path.lineTo(10, size.height);
    path.arcToPoint(Offset(0, size.height - 10), radius: Radius.circular(10));
    path.lineTo(0, 10);
    path.arcToPoint(Offset(10, 0), radius: Radius.circular(10));
    path.close();

    canvas.drawPath(path, fillPaint);
    canvas.drawPath(path, borderPaint);
    canvas.drawShadow(path, color, 1, true);
  }

  @override
  bool shouldRepaint(CustomPainter oldDelegate) => false;
}

class _AbsorbPointer extends SingleChildRenderObjectWidget {
  final RenderBox? exclusion;
  const _AbsorbPointer({
    required this.exclusion
  });

  @override
  _RenderAbsorbPointer createRenderObject(BuildContext context) => _RenderAbsorbPointer(
    exclusion: exclusion
  );
}

class _RenderAbsorbPointer extends RenderProxyBox {
  final RenderBox? exclusion;
  _RenderAbsorbPointer({
    required this.exclusion,
    RenderBox? child
  }) : super(child);

  @override
  bool hitTest(BoxHitTestResult result, { required Offset position }) {
    final exclusion = this.exclusion;
    if(exclusion == null) {
      return size.contains(position);
    }
    
    // 32 is the height of the title bar (need this offset as the overlay area doesn't include it)
    // Not an optimal solution but it works (calculating it is kind of complicated)
    position = Offset(position.dx, position.dy);
    final exclusionPosition = exclusion.localToGlobal(Offset.zero);
    final exclusionSize = Rect.fromLTRB(
      exclusionPosition.dx,
      exclusionPosition.dy,
      exclusionPosition.dx + exclusion.size.width,
      exclusionPosition.dy + exclusion.size.height
    );
    return !exclusionSize.contains(position);
  }

  @override
  void visitChildrenForSemantics(RenderObjectVisitor visitor) => super.visitChildrenForSemantics(visitor);

  @override
  void describeSemanticsConfiguration(SemanticsConfiguration config) {
    super.describeSemanticsConfiguration(config);
    config.isBlockingUserActions = true;
  }
}
