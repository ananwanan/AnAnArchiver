import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:litearc_binding/litearc_binding.dart';

// Frontend values only. No retained task, owned FFI result or metadata index.
class TaskHistoryController extends ChangeNotifier {
  TaskHistoryController(Stream<TaskProgress> progress) {
    _subscription = progress.listen((p) {
      if (_disposed || p.taskId == 0) return;
      final summary = TaskProgress(
        p.operation,
        p.state,
        p.entries,
        p.elapsedMs,
        taskId: p.taskId,
        bytes: p.bytes,
        totalBytes: p.totalBytes,
        totalEntries: p.totalEntries,
        bytesPerSecond: p.bytesPerSecond,
        currentFile: p.currentFile,
        phase: p.phase,
        error: p.error,
      );
      tasks.remove(p.taskId);
      tasks[p.taskId] = summary;
      final destination = destinationFor?.call(p);
      if (destination != null) destinations[p.taskId] = destination;
      while (tasks.length > 32) {
        final retired = tasks.entries
            .where((e) => e.value.terminal)
            .firstOrNull;
        if (retired == null) break;
        tasks.remove(retired.key);
        destinations.remove(retired.key);
      }
      notifyListeners();
    });
  }
  final tasks = <int, TaskProgress>{};
  final destinations = <int, String>{};
  String? Function(TaskProgress)? destinationFor;
  late final StreamSubscription<TaskProgress> _subscription;
  bool _disposed = false;
  void clear() {
    tasks.removeWhere((_, value) => value.terminal);
    destinations.removeWhere((id, _) => !tasks.containsKey(id));
    notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    destinationFor = null;
    unawaited(_subscription.cancel());
    super.dispose();
  }
}
