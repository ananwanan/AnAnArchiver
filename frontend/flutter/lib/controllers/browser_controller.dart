import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:litearc_binding/litearc_binding.dart';

class RowSelection {
  final List<(int, int)> ranges = []; // half-open query positions, at most 4096
  int get count => ranges.fold(0, (n, r) => n + r.$2 - r.$1);
  bool contains(int index) => ranges.any((r) => index >= r.$1 && index < r.$2);
  void clear() => ranges.clear();
  void replace(int first, int end) {
    ranges.clear();
    if (end > first) ranges.add((first, end));
  }

  bool toggle(int index) {
    final next = List<(int, int)>.of(ranges);
    final at = next.indexWhere((r) => index >= r.$1 && index < r.$2);
    if (at >= 0) {
      final r = next.removeAt(at);
      if (r.$1 < index) next.add((r.$1, index));
      if (index + 1 < r.$2) next.add((index + 1, r.$2));
    } else {
      next.add((index, index + 1));
    }
    next.sort((a, b) => a.$1.compareTo(b.$1));
    final merged = <(int, int)>[];
    for (final r in next) {
      if (merged.isNotEmpty && merged.last.$2 == r.$1) {
        final last = merged.removeLast();
        merged.add((last.$1, r.$2));
      } else {
        merged.add(r);
      }
    }
    if (merged.length > 4096) return false;
    ranges
      ..clear()
      ..addAll(merged);
    return true;
  }
}

class BrowserController extends ChangeNotifier {
  BrowserController(this.transport) {
    _subscription = transport.progress.listen((p) {
      if (!_disposed && !isHome) {
        progress = p;
        notifyListeners();
      }
    });
  }
  final BrowserTransport transport;
  late final StreamSubscription<TaskProgress> _subscription;
  static const maxPages = 8, maxFolderWindows = 8;
  final pages = <int, BrowserPage>{};
  final folderWindows = <String, BrowserPage>{};
  final expanded = <String>{};
  final selection = RowSelection();
  final _folderPending = <String>{};
  int generation = 0,
      query = 0,
      archive = 0,
      total = 0,
      entries = 0,
      indexBytes = 0;
  int sort = 0, selectionHandle = 0, _selectionVersion = 0, _anchor = 0;
  String? path;
  String folder = '', search = '', treeRoot = '';
  bool descending = false, busy = false, _disposed = false, _paging = false;
  BrowserFailure? error;
  TaskProgress? progress;
  Timer? _searchTimer, _selectionTimer;
  int _wanted = 0;
  bool get isHome => path == null;
  int get cachedRows => pages.values.fold(0, (n, p) => n + p.rows.length);
  int get cachedBytes => pages.values.fold(0, (n, p) => n + p.transferBytes);
  bool _valid(int token) => !_disposed && token == generation;
  void _resetView() {
    generation++;
    query = 0;
    total = 0;
    pages.clear();
    selection.clear();
    selectionHandle = 0;
    _selectionVersion++;
    _selectionTimer?.cancel();
    error = null;
    progress = null;
    _wanted = 0;
  }

  void _failure(Object e, int token) {
    if (_valid(token) && (e is! BrowserFailure || e.status != 9)) {
      error = e is BrowserFailure ? e : BrowserFailure(7, 0, '$e');
      notifyListeners();
    }
  }

  Future<void> open(String source) async {
    _searchTimer?.cancel();
    _resetView();
    final token = generation;
    path = source;
    folder = '';
    search = '';
    archive = 0;
    entries = 0;
    indexBytes = 0;
    folderWindows.clear();
    treeRoot = '';
    expanded.clear();
    busy = true;
    notifyListeners();
    try {
      final r = await transport.request('open', {'path': source});
      if (!_valid(token)) return;
      archive = r.archive;
      entries = r.entryCount;
      indexBytes = r.indexBytes;
      await refresh();
      if (!_disposed && archive != 0) unawaited(loadFolders(''));
    } catch (e) {
      _failure(e, token);
    } finally {
      if (_valid(token)) {
        busy = false;
        notifyListeners();
      }
    }
  }

  Future<void> refresh() async {
    if (archive == 0 || _disposed) return;
    _resetView();
    final token = generation;
    busy = true;
    notifyListeners();
    try {
      final r = await transport.request('query', {
        'folder': folder,
        'search': search,
        'sort': sort,
        'descending': descending,
      });
      if (!_valid(token)) return;
      query = r.query;
      total = r.total;
      busy = false;
      notifyListeners();
      await ensureWindow(0);
    } catch (e) {
      _failure(e, token);
    } finally {
      if (_valid(token)) {
        busy = false;
        notifyListeners();
      }
    }
  }

  Future<void> navigate(String next) async {
    _searchTimer?.cancel();
    folder = next;
    search = '';
    await refresh();
  }

  Future<void> up() => navigate(parent(folder));
  static String parent(String value) {
    final parts = value.split('/').where((p) => p.isNotEmpty).toList();
    if (parts.isNotEmpty) parts.removeLast();
    return parts.join('/');
  }

  void setSearch(String value) {
    if (search == value) return;
    search = value;
    // Invalidate the visible state immediately, before the debounced native query.
    _resetView();
    busy = true;
    notifyListeners();
    _searchTimer?.cancel();
    _searchTimer = Timer(
      const Duration(milliseconds: 180),
      () => unawaited(refresh()),
    );
  }

  Future<void> setSort(int column) {
    if (sort == column) {
      descending = !descending;
    } else {
      sort = column;
      descending = false;
    }
    return refresh();
  }

  BrowserRow? rowAt(int index) {
    for (final p in pages.values) {
      if (index >= p.offset && index < p.offset + p.rows.length) {
        return p.rows[index - p.offset];
      }
    }
    return null;
  }

  Future<void> ensureWindow(int first) async {
    _wanted = first.clamp(0, total > 0 ? total - 1 : 0);
    if (_paging || query == 0 || total == 0 || _disposed) return;
    _paging = true;
    var fetched = 0;
    try {
      while (!_disposed && query != 0 && total > 0 && fetched < maxPages) {
        final token = generation;
        final start = _wanted;
        // Fill a viewport plus a small prefetch. Variable byte-limited native
        // pages are supported: the next request starts after actual rows.
        int? missing;
        for (var i = start; i < total && i < start + 96; i++) {
          if (rowAt(i) == null) {
            missing = i;
            break;
          }
        }
        if (missing == null) break;
        try {
          final p = await transport.request('page', {
            'query': query,
            'offset': missing,
          });
          if (!_valid(token)) continue;
          if (p.query != query || p.archive != archive) continue;
          if (p.rows.isEmpty) {
            throw const BrowserFailure(7, 0, 'Empty page before EOF');
          }
          fetched++;
          pages.remove(p.offset);
          pages[p.offset] = p;
          while (pages.length > maxPages) {
            pages.remove(pages.keys.first);
          }
          notifyListeners();
        } catch (e) {
          _failure(e, token);
          if (_valid(token)) break;
        }
      }
    } finally {
      _paging = false;
    }
  }

  Future<void> loadFolders(String parentFolder, {int offset = 0}) async {
    if (archive == 0 || _disposed) return;
    final key = '$parentFolder\u0000$offset';
    if (_folderPending.contains(key) || _folderPending.length >= 2) return;
    _folderPending.add(key);
    final currentArchive = archive;
    try {
      final p = await transport.request('folders', {
        'folder': parentFolder,
        'offset': offset,
      });
      if (_disposed || currentArchive != archive || p.archive != archive) {
        return;
      }
      folderWindows.remove(parentFolder);
      folderWindows[parentFolder] = p;
      while (folderWindows.length > maxFolderWindows) {
        final old = folderWindows.keys.first;
        folderWindows.remove(old);
        expanded.remove(old);
        if (old == treeRoot) treeRoot = parentFolder;
      }
      notifyListeners();
    } catch (e) {
      if (!_disposed && currentArchive == archive) _failure(e, generation);
    } finally {
      _folderPending.remove(key);
    }
  }

  Future<void> focusTree(String value) async {
    treeRoot = value;
    await loadFolders(value);
    if (!_disposed) notifyListeners();
  }

  Future<void> expand(String value) async {
    if (expanded.remove(value)) {
      notifyListeners();
      return;
    }
    if (expanded.length >= maxFolderWindows) expanded.remove(expanded.first);
    expanded.add(value);
    notifyListeners();
    await loadFolders(value);
  }

  void choose(int index, {bool additive = false, bool extend = false}) {
    if (query == 0 ||
        busy ||
        index < 0 ||
        index >= total ||
        rowAt(index) == null) {
      return;
    }
    if (extend) {
      selection.replace(
        index < _anchor ? index : _anchor,
        (index > _anchor ? index : _anchor) + 1,
      );
    } else if (additive) {
      if (!selection.toggle(index)) {
        error = const BrowserFailure(6, 14, 'Selection range limit');
      }
      _anchor = index;
    } else {
      selection.replace(index, index + 1);
      _anchor = index;
    }
    _submitSelection();
  }

  void selectAll() {
    if (query == 0 || busy) return;
    selection.replace(0, total);
    _submitSelection();
  }

  void _submitSelection() {
    selectionHandle = 0;
    final version = ++_selectionVersion;
    final token = generation;
    _selectionTimer?.cancel();
    _selectionTimer = Timer(const Duration(milliseconds: 80), () async {
      try {
        final all = selection.count == total && total > 0;
        final p = await transport.request('select', {
          'query': query,
          'all': all,
          'ranges': all
              ? <List<int>>[]
              : selection.ranges.map((r) => [r.$1, r.$2 - r.$1]).toList(),
        });
        if (_valid(token) && version == _selectionVersion && p.query == query) {
          selectionHandle = p.selection;
          notifyListeners();
        }
      } catch (e) {
        if (version == _selectionVersion) _failure(e, token);
      }
    });
    notifyListeners();
  }

  Future<void> cancel() async {
    _searchTimer?.cancel();
    _resetView();
    busy = false;
    notifyListeners();
    try {
      await transport.request('cancel');
    } catch (e) {
      _failure(e, generation);
    }
  }

  Future<void> closeArchive() async {
    _searchTimer?.cancel();
    _resetView();
    path = null;
    archive = 0;
    entries = 0;
    folder = '';
    search = '';
    folderWindows.clear();
    treeRoot = '';
    expanded.clear();
    busy = false;
    notifyListeners();
    try {
      await transport.request('close');
    } catch (e) {
      _failure(e, generation);
    }
  }

  Future<void> shutdown() async {
    if (!_disposed) dispose();
    await transport.close();
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    generation++;
    _searchTimer?.cancel();
    _selectionTimer?.cancel();
    unawaited(_subscription.cancel());
    unawaited(transport.close());
    super.dispose();
  }
}
