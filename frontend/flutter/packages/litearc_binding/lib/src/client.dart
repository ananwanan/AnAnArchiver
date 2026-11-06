import 'dart:async';
import 'dart:collection';
import 'dart:isolate';
import 'dart:io';

import 'models.dart';
import 'native_engine.dart';

const _stale = BrowserFailure(9, 12, 'Superseded or closed');

// Native cleanup is asynchronous. Only capacity failures are transient;
// genuine allocation/index/input-budget failures must remain errors.
bool _capacityFailure(BrowserFailure e) =>
    e.status == 4 ||
    (e.status == 6 &&
        (e.code == 0 ||
            e.message ==
                'Task limit reached; cancel or release an existing task'));

class BrowserClient implements BrowserTransport {
  BrowserClient._();
  final _events = StreamController<TaskProgress>.broadcast();
  final _taskEvents = StreamController<TaskProgress>.broadcast();
  Stream<TaskProgress> get taskEvents => _taskEvents.stream;
  final _pending = <int, Completer<BrowserPage>>{};
  final _receive = ReceivePort();
  final _ready = Completer<void>();
  final _exited = Completer<void>();
  late SendPort _send;
  int _next = 0, _requestEpoch = 0;
  final _epochs = <int, int>{};
  bool _closed = false;
  Future<void>? _closing;
  static Future<BrowserClient> start({String? library}) async {
    final client = BrowserClient._();
    client._receive.listen(client._message);
    await Isolate.spawn(
      _worker,
      [client._receive.sendPort, library],
      onExit: client._receive.sendPort,
      onError: client._receive.sendPort,
      errorsAreFatal: true,
      debugName: 'LiteArc FFI',
    );
    await client._ready.future;
    return client;
  }

  void _message(dynamic message) {
    if (message is SendPort) {
      _send = message;
      _ready.complete();
      return;
    }
    if (message is List && message.length == 2 && message[0] is int) {
      if (message[1] is TaskProgress) {
        _send.send([message[0], 'progressAck', <String, Object?>{}]);
        if (!_closed) _taskEvents.add(message[1] as TaskProgress);
        if (!_closed &&
            ((message[1] as TaskProgress).isArchiveOperation ||
                _epochs[message[0]] == _requestEpoch)) {
          _events.add(message[1] as TaskProgress);
        }
        return;
      }
      _epochs.remove(message[0]);
      final c = _pending.remove(message[0]);
      if (message[1] is BrowserPage) {
        c?.complete(message[1] as BrowserPage);
      } else {
        c?.completeError(message[1] as Object);
      }
      return;
    }
    final error = message is BrowserFailure
        ? message
        : BrowserFailure(7, 0, 'Binding worker exited: $message');
    if (!_ready.isCompleted) _ready.completeError(error);
    for (final c in _pending.values) {
      c.completeError(error);
    }
    _pending.clear();
    _epochs.clear();
    if (!_exited.isCompleted) _exited.complete();
    _receive.close();
  }

  @override
  Stream<TaskProgress> get progress => _events.stream;
  @override
  Future<BrowserPage> request(
    String operation, [
    Map<String, Object?> args = const {},
  ]) {
    if (_closed) return Future.error(_stale);
    // UI callers are bounded too; don't turn the isolate mailbox into a cache.
    if (_pending.length >= 32 ||
        (_pending.length >= 28 &&
            !const {
              'resolveConflict',
              'cancelOperation',
              'cancel',
              'close',
            }.contains(operation))) {
      return Future.error(const BrowserFailure(6, 14, 'Binding queue limit'));
    }
    if (const {'open', 'query', 'close', 'cancel'}.contains(operation)) {
      _requestEpoch++;
    }
    final id = ++_next;
    _epochs[id] = _requestEpoch;
    final c = Completer<BrowserPage>();
    _pending[id] = c;
    _send.send([id, operation, Map<String, Object?>.of(args)]);
    return c.future;
  }

  @override
  Future<void> close() => _closing ??= _shutdown();
  Future<void> _shutdown() async {
    if (_closed) return;
    _closed = true;
    _send.send([++_next, 'shutdown', <String, Object?>{}]);
    await _exited.future;
    await _events.close();
    await _taskEvents.close();
  }
}

void _worker(List<Object?> init) {
  final send = init[0] as SendPort;
  try {
    _Worker(send, NativeEngine(init[1] as String?)).listen();
  } catch (e) {
    send.send(e is BrowserFailure ? e : BrowserFailure(7, 0, '$e'));
  }
}

class _Worker {
  _Worker(this.send, this.native);
  final SendPort send;
  final NativeEngine native;
  final inbox = ReceivePort();
  final queue = Queue<List>();
  bool running = false, shuttingDown = false;
  int epoch = 0;
  int operationTask = 0;
  bool operationCancelled = false;
  Future<void>? operationFuture;
  final progressInFlight = <int>{};
  final latestProgress = <int, TaskProgress>{};
  void progress(int request, TaskProgress value) {
    if (value.terminal) {
      finishProgress(request);
      send.send([request, value]);
    } else if (progressInFlight.add(request)) {
      send.send([request, value]);
    } else {
      latestProgress[request] = value;
    }
  }

  void finishProgress(int request) {
    progressInFlight.remove(request);
    latestProgress.remove(request);
  }

  void listen() {
    inbox.listen((dynamic value) {
      final message = value as List;
      final op = message[1] as String;
      if (op == 'progressAck') {
        progressInFlight.remove(message[0]);
        final latest = latestProgress.remove(message[0]);
        if (latest != null) progress(message[0] as int, latest);
        return;
      }
      if (shuttingDown) {
        send.send([message[0], _stale]);
        return;
      }
      if (op == 'resolveConflict' || op == 'cancelOperation') {
        try {
          if (op == 'cancelOperation') {
            operationCancelled = true;
            native.cancelTask(operationTask);
          } else {
            final args = Map<String, Object?>.from(message[2] as Map);
            if (operationTask == 0 ||
                args['task'] != operationTask ||
                operationCancelled) {
              throw _stale;
            }
            native.resolve(args);
          }
          send.send([message[0], const BrowserPage()]);
        } catch (e) {
          send.send([message[0], e]);
        }
        return;
      }
      if (const {
        'extractAll',
        'extractSelected',
        'compress',
        'export',
      }.contains(op)) {
        if (operationFuture != null) {
          send.send([
            message[0],
            const BrowserFailure(4, 14, 'Archive operation already active'),
          ]);
        } else {
          operationCancelled = false;
          operationFuture = runOperation(message);
        }
        return;
      }
      if (op == 'open' || op == 'close' || op == 'shutdown') {
        operationCancelled = true;
        native.cancelTask(operationTask);
      }
      if (op == 'open' ||
          op == 'query' ||
          op == 'close' ||
          op == 'cancel' ||
          op == 'shutdown') {
        epoch++;
        native.cancel();
        while (queue.isNotEmpty) {
          send.send([queue.removeFirst()[0], _stale]);
        }
      } else if (op == 'select') {
        final old = queue.where((m) => m[1] == 'select').toList();
        for (final m in old) {
          queue.remove(m);
          send.send([m[0], _stale]);
        }
      }
      if (op == 'shutdown') shuttingDown = true;
      if (queue.length >= 16) {
        send.send([
          message[0],
          const BrowserFailure(6, 14, 'Worker queue limit'),
        ]);
        return;
      }
      queue.add(message);
      unawaited(drain());
    });
    send.send(inbox.sendPort);
  }

  Future<void> runOperation(List message) async {
    // Yield before starting so operationFuture is assigned even on submission failure.
    await Future<void>.delayed(Duration.zero);
    final args = Map<String, Object?>.from(message[2] as Map);
    try {
      for (var retries = 0; ; retries++) {
        if (operationCancelled) {
          throw const BrowserFailure(8, 12, 'Operation cancelled');
        }
        try {
          operationTask = native.submitOperation(message[1] as String, args);
          break;
        } on BrowserFailure catch (e) {
          if (!_capacityFailure(e) || retries >= 100) rethrow;
          await Future<void>.delayed(const Duration(milliseconds: 20));
        }
      }
      args.remove('password');
      (message[2] as Map).remove('password');
      while (true) {
        if (operationCancelled) native.cancelTask(operationTask);
        final p = native.poll(
          message[1] as String,
          id: operationTask,
          throwTerminal: false,
        );
        progress(message[0] as int, p);
        if (p.terminal) {
          if (p.state != 5) {
            throw p.error ?? const BrowserFailure(8, 12, 'Operation cancelled');
          }
          send.send([message[0], const BrowserPage()]);
          break;
        }
        await Future<void>.delayed(const Duration(milliseconds: 50));
      }
    } catch (e) {
      send.send([
        message[0],
        e is BrowserFailure ? e : BrowserFailure(7, 0, '$e'),
      ]);
    } finally {
      args.remove('password');
      (message[2] as Map).remove('password');
      native.releaseTask(operationTask);
      finishProgress(message[0] as int);
      operationTask = 0;
      operationFuture = null;
    }
  }

  Future<void> drain() async {
    if (running) return;
    running = true;
    while (queue.isNotEmpty) {
      final m = queue.removeFirst();
      final op = m[1] as String;
      final version = epoch;
      try {
        if (op == 'shutdown') {
          await operationFuture;
          native.destroy();
          inbox.close();
          send.send([m[0], const BrowserPage()]);
          break;
        }
        if (op == 'close' || op == 'cancel') {
          if (op == 'close') {
            await operationFuture;
            native.closeArchive();
          }
          send.send([m[0], const BrowserPage()]);
          continue;
        }
        if (op == 'capabilities') {
          send.send([m[0], native.capabilities()]);
          continue;
        }
        if (op == 'exportControl') {
          send.send([
            m[0],
            native.exportControl(Map<String, Object?>.from(m[2] as Map)),
          ]);
          continue;
        }
        if (op == 'probe') {
          final source = (m[2] as Map)['path'] as String;
          final type = await FileSystemEntity.type(source);
          if (version != epoch) throw _stale;
          if (type == FileSystemEntityType.directory) {
            send.send([
              m[0],
              BrowserPage(classification: 'inputs', paths: [source]),
            ]);
            continue;
          }
        }
        if (op == 'open') await operationFuture;
        // One browser task beside one operation. A released task may still be
        // in native reclamation: bounded capacity retries yield to controls.
        for (var retries = 0; ; retries++) {
          if (version != epoch) throw _stale;
          try {
            native.submit(op, Map<String, Object?>.from(m[2] as Map));
            (m[2] as Map).remove('password');
            break;
          } on BrowserFailure catch (e) {
            if (!_capacityFailure(e) || retries >= 100) rethrow;
            await Future<void>.delayed(const Duration(milliseconds: 20));
          }
        }
        while (true) {
          if (version != epoch) throw _stale;
          final snapshot = native.poll(op, throwTerminal: false);
          progress(m[0] as int, snapshot);
          if (snapshot.terminal) {
            if (op == 'probe' && [3, 6].contains(snapshot.error?.code)) {
              send.send([
                m[0],
                BrowserPage(
                  classification: 'inputs',
                  paths: [(m[2] as Map)['path'] as String],
                ),
              ]);
              break;
            }
            if (op == 'probe' &&
                (snapshot.state == 5 ||
                    [4, 8, 9].contains(snapshot.error?.code))) {
              send.send([m[0], const BrowserPage(classification: 'archive')]);
              break;
            }
            if (snapshot.state != 5) {
              throw snapshot.error ??
                  const BrowserFailure(8, 12, 'Task cancelled');
            }
            break;
          }
          await Future<void>.delayed(const Duration(milliseconds: 50));
        }
        if (version != epoch) throw _stale;
        if (op != 'probe') send.send([m[0], native.result()]);
      } catch (e) {
        send.send([m[0], e is BrowserFailure ? e : BrowserFailure(7, 0, '$e')]);
      } finally {
        (m[2] as Map).remove('password');
        if (native.task != 0) {
          // Replacements still publish the actual terminal snapshot for history.
          // Controls remain serviced while this lane waits for cancellation.
          var last = native.poll(op, throwTerminal: false);
          if (!last.terminal) native.cancel();
          while (!last.terminal) {
            await Future<void>.delayed(const Duration(milliseconds: 20));
            last = native.poll(op, throwTerminal: false);
          }
          progress(m[0] as int, last);
        }
        finishProgress(m[0] as int);
        native.release();
      }
    }
    running = false;
  }
}
