import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:litearc_binding/litearc_binding.dart';

import '../platform/settings_store.dart';
import 'browser_controller.dart';

// One pending external request. Latest request wins while opening; duplicate
// paths coalesce. Explicit external opens cancel the old operation via Browser.
class DesktopOpenController {
  DesktopOpenController(this.browser);
  final BrowserController browser;
  String? _pending, _opening;
  bool _running = false, _closed = false;
  void openFiles(List<String> files) {
    if (_closed || files.isEmpty) return;
    if (files.length != 1 || !validRecent(files.single)) {
      browser.operations.reportFailure(
        const BrowserFailure(
          1,
          5,
          'System Open accepts one absolute file path per event',
        ),
      );
      return;
    }
    final path = files.single;
    if (recentKey(path) == recentKey(_pending ?? _opening ?? '')) return;
    _pending = path;
    if (_running) {
      unawaited(browser.cancel());
      return;
    }
    unawaited(_drain());
  }

  Future<void> _drain() async {
    _running = true;
    while (!_closed && _pending != null) {
      _opening = _pending;
      _pending = null;
      await browser.open(_opening!);
    }
    _opening = null;
    _running = false;
  }

  void close() {
    _closed = true;
    _pending = null;
  }
}

class ExportController extends ChangeNotifier {
  ExportController(this.browser, this.parent) {
    browser.addListener(_changed);
    _cleanupPoll = Timer.periodic(
      const Duration(seconds: 2),
      (_) => unawaited(_pollCleanup()),
    );
  }
  final BrowserController browser;
  final String parent;
  String? root;
  List<String> paths = const [];
  bool handedOff = false, preparing = false, _disposed = false;
  int? _archive, _selection;
  Timer? _expiry;
  Timer? _cleanupPoll;
  bool _storeReady = false, _checkingCleanup = false;
  Future<void> _pollCleanup() async {
    if (!_storeReady || _disposed || _checkingCleanup) return;
    _checkingCleanup = true;
    try {
      final errors = await browser.transport.request('exportControl', {
        'action': 6,
      });
      for (final error in errors.failures) {
        browser.operations.reportFailure(error);
      }
    } catch (e) {
      if (!_disposed) browser.operations.reportFailure(e);
    } finally {
      _checkingCleanup = false;
    }
  }

  void _changed() {
    if (root != null &&
        !preparing &&
        (_archive != browser.archive ||
            _selection != browser.selectionHandle)) {
      unawaited(invalidate());
    }
  }

  Future<void> invalidate() async {
    final old = root;
    root = null;
    paths = const [];
    _expiry?.cancel();
    if (!handedOff && old != null) {
      try {
        await browser.transport.request('exportControl', {
          'action': 4,
          'path': old,
        });
      } catch (e) {
        if (!_disposed) browser.operations.reportFailure(e);
      }
    }
    handedOff = false;
    if (!_disposed) notifyListeners();
  }

  Future<void> prepare() async {
    if (preparing || !browser.canExtractSelected || browser.operations.busy) {
      return;
    }
    preparing = true;
    notifyListeners();
    await invalidate();
    _archive = browser.archive;
    _selection = browser.selectionHandle;
    final query = browser.query;
    try {
      root = (await browser.transport.request('exportControl', {
        'action': 1,
        'path': parent,
      })).paths.single;
      _storeReady = true;
      await browser.operations.run('export', {
        'archive': _archive,
        'query': query,
        'selection': _selection,
        'destination': root,
      });
      if (_disposed ||
          browser.operations.error != null ||
          _archive != browser.archive ||
          _selection != browser.selectionHandle) {
        await invalidate();
        return;
      }
      paths = (await browser.transport.request('exportControl', {
        'action': 2,
        'path': root,
      })).paths;
      _expiry = Timer(
        const Duration(minutes: 10),
        () => unawaited(invalidate()),
      );
    } catch (e) {
      await invalidate();
      if (!_disposed) browser.operations.reportFailure(e);
    } finally {
      preparing = false;
      if (!_disposed) notifyListeners();
    }
  }

  Future<List<String>> handoff() async {
    if (root == null || paths.isEmpty || preparing) return const [];
    final output = paths;
    final current = root;
    // Set the hold before awaiting: navigation during this round trip must not
    // schedule deletion of a root that native code has already handed off.
    handedOff = true;
    _expiry?.cancel();
    await browser.transport.request('exportControl', {
      'action': 3,
      'path': current,
    });
    // Keep this root even if a page or the app closes during native copying.
    return output;
  }

  Future<BrowserPage> retained() async {
    final result = await browser.transport.request('exportControl', {
      'action': 7,
      'path': parent,
    });
    _storeReady = true;
    return result;
  }

  Future<void> release(String path) async {
    if (path == root) {
      handedOff = false;
      await invalidate();
    } else {
      await browser.transport.request('exportControl', {
        'action': 4,
        'path': path,
      });
    }
    final errors = await browser.transport.request('exportControl', {
      'action': 6,
    });
    for (final error in errors.failures) {
      browser.operations.reportFailure(error);
    }
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _expiry?.cancel();
    _cleanupPoll?.cancel();
    browser.removeListener(_changed);
    // Engine worker cleans unhanded roots; handed-off roots are durable.
    super.dispose();
  }
}
