class BrowserFailure implements Exception {
  const BrowserFailure(
    this.status,
    this.code,
    this.message, [
    this.backend = '',
    this.path = '',
  ]);
  final int status, code;
  final String message, backend, path;
  @override
  String toString() => 'BrowserFailure($status, $code): $message';
}

class BrowserRow {
  const BrowserRow({
    required this.identity,
    required this.index,
    required this.name,
    required this.path,
    required this.folder,
    required this.type,
    required this.flags,
    required this.size,
    required this.modified,
  });
  final int identity, index, type, flags, size, modified;
  final String name, path;
  final bool folder;
}

class BrowserPage {
  const BrowserPage({
    this.archive = 0,
    this.query = 0,
    this.selection = 0,
    this.entryCount = 0,
    this.folderCount = 0,
    this.indexBytes = 0,
    this.offset = 0,
    this.total = 0,
    this.transferBytes = 0,
    this.rows = const [],
  });
  final int archive,
      query,
      selection,
      entryCount,
      folderCount,
      indexBytes,
      offset,
      total,
      transferBytes;
  final List<BrowserRow> rows;
}

class TaskProgress {
  const TaskProgress(this.operation, this.state, this.entries, this.elapsedMs);
  final String operation;
  final int state, entries, elapsedMs;
}

abstract interface class BrowserTransport {
  Stream<TaskProgress> get progress;
  Future<BrowserPage> request(
    String operation, [
    Map<String, Object?> args = const {},
  ]);
  Future<void> close();
}
