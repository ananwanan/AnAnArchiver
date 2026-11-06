import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:litearc_binding/litearc_binding.dart';
import 'package:super_drag_and_drop/super_drag_and_drop.dart';

import '../controllers/desktop_controller.dart';
import '../l10n/app_localizations.dart';

class SystemFileDrop extends StatelessWidget {
  const SystemFileDrop({
    super.key,
    required this.child,
    required this.onFiles,
    required this.onError,
    required this.enabled,
  });
  final Widget child;
  final Future<void> Function(List<String>) onFiles;
  final ValueChanged<Object> onError;
  final bool enabled;
  @override
  Widget build(BuildContext context) => DropRegion(
    formats: [Formats.fileUri],
    hitTestBehavior: HitTestBehavior.opaque,
    onDropOver: (event) =>
        enabled &&
            event.session.items.isNotEmpty &&
            event.session.items.length <= 4096 &&
            event.session.items.every(
              (i) =>
                  i.localData != 'LiteArc export' &&
                  i.canProvide(Formats.fileUri),
            )
        ? DropOperation.copy
        : DropOperation.none,
    onPerformDrop: (event) async {
      if (!enabled) return;
      try {
        if (event.session.items.length > 4096) {
          throw const BrowserFailure(6, 14, 'Drop input count budget');
        }
        final paths = <String>[];
        var bytes = 0;
        final readingTime = Stopwatch()..start();
        for (final item in event.session.items) {
          final remainingMs = 10000 - readingTime.elapsedMilliseconds;
          if (remainingMs <= 0) {
            throw const BrowserFailure(6, 14, 'Drop URI read time budget');
          }
          final value = Completer<Uri?>();
          final reading = item.dataReader?.getValue(
            Formats.fileUri,
            value.complete,
            onError: value.completeError,
          );
          if (reading == null) {
            throw const BrowserFailure(1, 5, 'Filesystem URI unavailable');
          }
          final uri = await value.future.timeout(
            Duration(milliseconds: remainingMs),
          );
          if (uri == null || uri.scheme != 'file') {
            throw const BrowserFailure(
              1,
              5,
              'Only filesystem files or directories can be dropped',
            );
          }
          final path = uri.toFilePath();
          bytes += path.length * 4;
          if (path.length > 32768 || bytes > 4 * 1024 * 1024) {
            throw const BrowserFailure(6, 14, 'Drop path byte budget');
          }
          paths.add(path);
        }
        // Windows OLE waits for this callback before ending the native drop.
        // Never retain it while an archive task or a modal dialog needs input.
        // These are ordinary paths (virtual file payloads are not accepted).
        unawaited(
          onFiles(paths).catchError((Object e) {
            onError(e);
          }),
        );
      } catch (e) {
        onError(e);
      }
    },
    child: child,
  );
}

class ExportDragHandle extends StatefulWidget {
  const ExportDragHandle(this.controller, {super.key});
  final ExportController controller;
  @override
  State<ExportDragHandle> createState() => _ExportDragHandleState();
}

class _ExportDragHandleState extends State<ExportDragHandle> {
  final boundary = GlobalKey();
  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    final l = AppLocalizations.of(context)!;
    return BaseDraggableWidget(
      dragConfiguration: (location, session) async {
        try {
          final paths = await c.handoff();
          if (!mounted || paths.isEmpty) return null;
          final render =
              boundary.currentContext?.findRenderObject()
                  as RenderRepaintBoundary?;
          if (render == null || render.debugNeedsPaint) return null;
          final image = await render.toImage(pixelRatio: 1);
          final items = paths
              .map((path) {
                final item = DragItem(localData: 'LiteArc export')
                  ..add(Formats.fileUri(Uri.file(path)));
                return DragConfigurationItem(
                  item: item,
                  image: TargetedWidgetSnapshot(
                    WidgetSnapshot.image(image.clone()),
                    Rect.fromLTWH(
                      location.dx,
                      location.dy,
                      image.width.toDouble(),
                      image.height.toDouble(),
                    ),
                  ),
                );
              })
              .toList(growable: false);
          image.dispose();
          return DragConfiguration(
            items: items,
            allowedOperations: [DropOperation.copy],
          );
        } catch (e) {
          c.browser.operations.reportFailure(e);
          return null;
        }
      },
      child: RepaintBoundary(
        key: boundary,
        child: Semantics(
          label: l.dragReady,
          child: MouseRegion(
            cursor: SystemMouseCursors.grab,
            child: Chip(
              avatar: const Icon(Icons.drag_indicator, size: 18),
              label: Text(l.dragReady),
            ),
          ),
        ),
      ),
    );
  }
}
