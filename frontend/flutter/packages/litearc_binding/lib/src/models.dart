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
    this.capabilities = const [],
    this.paths = const [],
    this.failures = const [],
    this.classification = '',
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
  final List<CompressionCapability> capabilities;
  final List<String> paths;
  final List<BrowserFailure> failures;
  final String classification;
}

class TaskProgress {
  const TaskProgress(
    this.operation,
    this.state,
    this.entries,
    this.elapsedMs, {
    this.taskId = 0,
    this.bytes = 0,
    this.totalBytes,
    this.totalEntries,
    this.bytesPerSecond = 0,
    this.currentFile = '',
    this.phase = 0,
    this.conflict,
    this.error,
  });
  final String operation;
  final int state, entries, elapsedMs;
  final int taskId, bytes, phase;
  final int? totalBytes, totalEntries;
  final double bytesPerSecond;
  final String currentFile;
  final TaskConflict? conflict;
  final BrowserFailure? error;
  bool get terminal => state == 4 || state == 5 || state == 6;
  bool get isArchiveOperation =>
      operation == 'extractAll' ||
      operation == 'extractSelected' ||
      operation == 'compress' ||
      operation == 'export';
}

class TaskConflict {
  const TaskConflict(this.id, this.path, this.destination);
  final int id;
  final String path, destination;
}

class CompressionCapability {
  const CompressionCapability({
    required this.format,
    required this.method,
    required this.formatName,
    required this.methodName,
    required this.extension,
    required this.hasLevels,
    required this.minLevel,
    required this.maxLevel,
    required this.defaultLevel,
    required this.permissions,
    required this.timestamps,
  });
  final int format, method, minLevel, maxLevel, defaultLevel;
  final String formatName, methodName, extension;
  final bool hasLevels, permissions, timestamps;
}

abstract interface class BrowserTransport {
  Stream<TaskProgress> get progress;
  Future<BrowserPage> request(
    String operation, [
    Map<String, Object?> args = const {},
  ]);
  Future<void> close();
}
