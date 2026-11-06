import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:litearc_binding/litearc_binding.dart';

// One operation alongside the bounded browser lane; no native pointers or IO.
class OperationController extends ChangeNotifier {
  OperationController(this.transport) {
    _subscription = transport.progress.listen((p) {
      if (!_disposed && busy && p.isArchiveOperation) {
        progress = p;
        notifyListeners();
      }
    });
  }
  final BrowserTransport transport;
  late final StreamSubscription<TaskProgress> _subscription;
  TaskProgress? progress;
  BrowserFailure? error, passwordError;
  bool busy = false, _disposed = false;
  String? operation, destination;
  int _generation = 0;
  Completer<String?>? _password;
  // Widgets clear their controllable text buffer synchronously, including exit.
  final passwordReset = ValueNotifier<int>(0);
  bool get waitingForPassword => _password != null;
  bool get visible =>
      busy || progress != null || error != null || waitingForPassword;
  void reportFailure(Object e) {
    if (_disposed) return;
    error = e is BrowserFailure ? e : BrowserFailure(7, 0, '$e');
    notifyListeners();
  }

  Future<String?> requestPassword(BrowserFailure failure) async {
    if (_disposed) return null;
    answerPassword(null);
    passwordError = failure;
    final pending = Completer<String?>();
    _password = pending;
    notifyListeners();
    return pending.future;
  }

  void answerPassword(String? value) {
    if (_disposed) return;
    _completePassword(value);
    notifyListeners();
  }

  void _completePassword(String? value) {
    passwordReset.value++;
    final pending = _password;
    _password = null;
    passwordError = null;
    pending?.complete(value);
  }

  Future<void> run(String op, Map<String, Object?> input) async {
    if (_disposed) return;
    if (busy || waitingForPassword) {
      throw const BrowserFailure(4, 14, 'Archive operation already active');
    }
    final options = Map<String, Object?>.of(input);
    final token = ++_generation;
    operation = op;
    destination = options['destination'] as String?;
    busy = true;
    error = null;
    progress = null;
    notifyListeners();
    try {
      while (!_disposed && token == _generation) {
        try {
          // request sends synchronously; remove our reference before awaiting.
          final result = transport.request(op, Map.of(options));
          options.remove('password');
          await result;
          error = null;
          break;
        } on BrowserFailure catch (e) {
          if (_disposed) break;
          if (token != _generation) {
            error = e;
            break;
          }
          if (e.code != 8 && e.code != 9) rethrow;
          String? password = await requestPassword(e);
          if (password == null || _disposed || token != _generation) {
            password = null;
            error = const BrowserFailure(8, 12, 'Password entry cancelled');
            break;
          }
          options['password'] = password;
          password = null;
          progress = null;
        }
      }
    } catch (e) {
      if (!_disposed) {
        error = e is BrowserFailure ? e : BrowserFailure(7, 0, '$e');
      }
    } finally {
      options.remove('password');
      if (!_disposed) {
        busy = false;
        notifyListeners();
      }
    }
  }

  Future<void> resolve(TaskProgress snapshot, int overwrite, bool all) async {
    final conflict = snapshot.conflict;
    if (_disposed ||
        !busy ||
        conflict == null ||
        progress?.taskId != snapshot.taskId ||
        progress?.conflict?.id != conflict.id) {
      throw const BrowserFailure(9, 5, 'Stale conflict answer');
    }
    await transport.request('resolveConflict', {
      'task': snapshot.taskId,
      'conflict': conflict.id,
      'overwrite': overwrite,
      'all': all,
    });
  }

  Future<void> cancel() async {
    if (_disposed) return;
    _generation++;
    answerPassword(null);
    if (busy) {
      await transport.request('cancelOperation');
    }
  }

  void dismiss() {
    if (_disposed || busy || waitingForPassword) return;
    progress = null;
    error = null;
    operation = null;
    notifyListeners();
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _generation++;
    _completePassword(null);
    passwordReset.dispose();
    unawaited(_subscription.cancel());
    super.dispose();
  }
}
