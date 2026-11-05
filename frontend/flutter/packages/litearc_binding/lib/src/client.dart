import 'dart:async';
import 'dart:collection';
import 'dart:isolate';

import 'models.dart';
import 'native_engine.dart';

const _stale = BrowserFailure(9, 12, 'Superseded or closed');

class BrowserClient implements BrowserTransport {
  BrowserClient._();
  final _events = StreamController<TaskProgress>.broadcast();
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
        if (!_closed && _epochs[message[0]] == _requestEpoch)
          _events.add(message[1] as TaskProgress);
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
    if (_pending.length >= 32)
      return Future.error(const BrowserFailure(6, 14, 'Binding queue limit'));
    if (const {'open', 'query', 'close', 'cancel'}.contains(operation))
      _requestEpoch++;
    final id = ++_next;
    _epochs[id] = _requestEpoch;
    final c = Completer<BrowserPage>();
    _pending[id] = c;
    _send.send([id, operation, args]);
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
  void listen() {
    inbox.listen((dynamic value) {
      final message = value as List;
      final op = message[1] as String;
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
      if (shuttingDown) {
        send.send([message[0], _stale]);
        return;
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

  Future<void> drain() async {
    if (running) return;
    running = true;
    while (queue.isNotEmpty) {
      final m = queue.removeFirst();
      final op = m[1] as String;
      final version = epoch;
      try {
        if (op == 'shutdown') {
          native.destroy();
          inbox.close();
          send.send([m[0], const BrowserPage()]);
          break;
        }
        if (op == 'close' || op == 'cancel') {
          if (op == 'close') native.closeArchive();
          send.send([m[0], const BrowserPage()]);
          continue;
        }
        // One active and one retained task. A released task may still be in
        // native reclamation: bounded busy retries yield to cancellation input.
        for (var retries = 0; ; retries++) {
          if (version != epoch) throw _stale;
          try {
            native.submit(op, Map<String, Object?>.from(m[2] as Map));
            break;
          } on BrowserFailure catch (e) {
            if (e.status != 4 || retries >= 100) rethrow;
            await Future<void>.delayed(const Duration(milliseconds: 20));
          }
        }
        while (true) {
          if (version != epoch) throw _stale;
          final progress = native.poll(op);
          send.send([m[0], progress]);
          if (progress.state == 5) break;
          await Future<void>.delayed(const Duration(milliseconds: 50));
        }
        if (version != epoch) throw _stale;
        send.send([m[0], native.result()]);
      } catch (e) {
        send.send([m[0], e is BrowserFailure ? e : BrowserFailure(7, 0, '$e')]);
      } finally {
        native.release();
      }
    }
    running = false;
  }
}
