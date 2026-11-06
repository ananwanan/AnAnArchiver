import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:ffi/ffi.dart';
import 'package:path/path.dart' as p;

Future<void> _settingsLane = Future<void>.value();
Future<T> _settingsWork<T>(T Function() operation) {
  final result = _settingsLane.then((_) => Isolate.run(operation));
  _settingsLane = result.then<void>(
    (_) {},
    onError: (Object _, StackTrace _) {},
  );
  return result;
}

// Only these public preferences cross the worker boundary. Never serialize a
// controller, operation request, password, conflict or native task snapshot.
class Preferences {
  const Preferences({
    this.theme = 0,
    this.language = 'system',
    this.recent = const [],
    this.width = 1280,
    this.height = 720,
    this.maximized = false,
    this.revision = 0,
    this.migration = 'none',
  });
  final int theme, revision;
  final String language, migration;
  final List<String> recent;
  final int width, height;
  final bool maximized;
  Map<String, Object?> toJson() => {
    'schema': 1,
    'revision': revision,
    'theme': theme,
    'language': language,
    'recent': recent,
    'width': width,
    'height': height,
    'maximized': maximized,
    'qtMigration': migration,
  };
  factory Preferences.parse(Map<String, dynamic> data) {
    const fields = {
      'schema',
      'revision',
      'theme',
      'language',
      'recent',
      'width',
      'height',
      'maximized',
      'qtMigration',
    };
    if (data.keys.any((key) => !fields.contains(key)) ||
        data['schema'] != 1 ||
        data['revision'] is! int ||
        (data['revision'] as int) < 0 ||
        ![0, 1, 2].contains(data['theme']) ||
        !['system', 'en', 'zh-CN'].contains(data['language']) ||
        data['recent'] is! List ||
        (data['recent'] as List).length > 20 ||
        (data['recent'] as List).any((v) => v is! String || !validRecent(v)) ||
        data['width'] is! int ||
        data['height'] is! int ||
        (data['width'] as int) < 640 ||
        (data['width'] as int) > 7680 ||
        (data['height'] as int) < 480 ||
        (data['height'] as int) > 4320 ||
        data['maximized'] is! bool ||
        data['qtMigration'] is! String) {
      throw const FormatException(
        'Unsupported or damaged preferences; original preserved',
      );
    }
    return Preferences(
      theme: data['theme'] as int,
      language: data['language'] as String,
      recent: (data['recent'] as List).cast<String>(),
      width: data['width'] as int,
      height: data['height'] as int,
      maximized: data['maximized'] as bool,
      revision: data['revision'] as int,
      migration: data['qtMigration'] as String,
    );
  }
}

bool validRecent(String path) =>
    path.isNotEmpty &&
    path.length <= 32768 &&
    !path.contains('\u0000') &&
    p.isAbsolute(path);
String recentKey(String path) => Platform.isWindows
    ? p
          .normalize(path)
          .replaceFirst(RegExp(r'^\\\\\?\\UNC\\', caseSensitive: false), r'\\')
          .replaceFirst(RegExp(r'^\\\\\?\\'), '')
          .toLowerCase()
    : p.normalize(path);
List<String> rememberRecent(List<String> recent, String path) {
  if (!validRecent(path)) return recent;
  return [
    p.normalize(path),
    ...recent.where((v) => recentKey(v) != recentKey(path)),
  ].take(20).toList(growable: false);
}

class SettingsStore {
  SettingsStore(this.directory, {this.legacyFixture});
  final String directory;
  // Explicit test input, never discovered in production.
  final Map<String, Object?>? legacyFixture;
  static SettingsStore system() {
    final env = Platform.environment;
    final base = Platform.isWindows
        ? env['APPDATA']
        : Platform.isMacOS
        ? p.join(env['HOME']!, 'Library', 'Application Support')
        : env['XDG_CONFIG_HOME'] ?? p.join(env['HOME']!, '.config');
    if (base == null || !p.isAbsolute(base)) {
      throw const FileSystemException(
        'User configuration location unavailable',
      );
    }
    return SettingsStore(p.join(base, 'AnAnArchiver', 'flutter'));
  }

  Future<Preferences> load() => _settingsWork(() => _transaction(null, null));
  Future<Preferences> save(Preferences value, int revision) =>
      _settingsWork(() => _transaction(value, revision));
  Preferences _transaction(Preferences? value, int? revision) {
    final dir = Directory(directory)..createSync(recursive: true);
    final lock = File(p.join(dir.path, 'preferences.lock'))
        .openSync(mode: FileMode.append);
    try {
      final waiting = Stopwatch()..start();
      while (true) {
        try {
          lock.lockSync(FileLock.exclusive);
          break;
        } on FileSystemException {
          if (waiting.elapsedMilliseconds >= 2000) {
            throw const FileSystemException(
              'Preferences are locked by another process; original preserved',
            );
          }
          sleep(const Duration(milliseconds: 20));
        }
      }
      final file = File(p.join(dir.path, 'preferences.json'));
      Preferences current;
      if (file.existsSync()) {
        if (file.lengthSync() > 2 * 1024 * 1024) {
          throw const FormatException(
            'Preferences exceed size budget; original preserved',
          );
        }
        current = Preferences.parse(
          jsonDecode(file.readAsStringSync()) as Map<String, dynamic>,
        );
      } else {
        final legacy = legacyFixture ?? readLegacy();
        final theme = legacy['theme'];
        final recent = legacy['recent'];
        if ((theme != null && ![0, 1, 2].contains(theme)) ||
            (recent != null &&
                (recent is! List ||
                    recent.length > 20 ||
                    recent.any((v) => v is! String || !validRecent(v))))) {
          throw const FormatException(
            'Unknown Qt preference data; migration not marked complete',
          );
        }
        var paths = <String>[];
        for (final path in ((recent as List?) ?? []).reversed) {
          paths = rememberRecent(paths, path as String);
        }
        current = Preferences(
          theme: theme as int? ?? 0,
          recent: paths,
          migration: legacy.isEmpty ? 'absent' : 'imported',
        );
        _write(file, current);
      }
      if (value == null) return current;
      if (current.revision != revision) {
        throw const FileSystemException(
          'Preferences changed in another process; reload before saving',
        );
      }
      final data = value.toJson()
        ..['revision'] = current.revision + 1
        ..['qtMigration'] = current.migration;
      final next = Preferences.parse(data);
      _write(file, next);
      return next;
    } finally {
      lock.closeSync();
    }
  }

  void _write(File file, Preferences value) {
    final body = '${jsonEncode(value.toJson())}\n';
    if (utf8.encode(body).length > 2 * 1024 * 1024) {
      throw const FormatException(
        'Encoded preferences exceed size budget; original preserved',
      );
    }
    final temporary = File('${file.path}.new');
    temporary.writeAsStringSync(body, flush: true);
    if (Platform.isWindows) {
      using((a) {
        final move = DynamicLibrary.open('kernel32.dll')
            .lookupFunction<
              Int32 Function(Pointer<Utf16>, Pointer<Utf16>, Uint32),
              int Function(Pointer<Utf16>, Pointer<Utf16>, int)
            >('MoveFileExW');
        if (move(
              temporary.path.toNativeUtf16(allocator: a),
              file.path.toNativeUtf16(allocator: a),
              0x1 | 0x8,
            ) ==
            0) {
          throw FileSystemException(
            'Atomic preference replacement failed',
            file.path,
          );
        }
      });
    } else {
      temporary.renameSync(file.path);
    }
  }
}

Map<String, Object?> readLegacy() {
  if (Platform.isWindows) return _windowsLegacy();
  final env = Platform.environment;
  if (Platform.isMacOS) {
    // QSettings comify("AnAnArchiver") -> com.ananarchiver.AnAnArchiver.
    final file = File(
      p.join(
        env['HOME']!,
        'Library',
        'Preferences',
        'com.ananarchiver.AnAnArchiver.plist',
      ),
    );
    if (!file.existsSync()) return {};
    if (file.lengthSync() > 2 * 1024 * 1024) {
      throw const FormatException('Qt plist size budget');
    }
    final result = Process.runSync('/usr/bin/plutil', [
      '-convert',
      'json',
      '-o',
      '-',
      file.path,
    ]);
    if (result.exitCode != 0) {
      throw const FormatException('Cannot decode Qt preferences');
    }
    final data = jsonDecode(result.stdout as String) as Map<String, dynamic>;
    return {
      if (data.containsKey('appearance.theme'))
        'theme': data['appearance.theme'],
      if (data.containsKey('archives.recent'))
        'recent': data['archives.recent'],
    };
  }
  final file = File(
    p.join(
      env['XDG_CONFIG_HOME'] ?? p.join(env['HOME']!, '.config'),
      'AnAnArchiver',
      'AnAnArchiver.conf',
    ),
  );
  if (!file.existsSync()) return {};
  if (file.lengthSync() > 2 * 1024 * 1024) {
    throw const FormatException('Qt INI size budget');
  }
  return parseQtIni(file.readAsStringSync());
}

// Strict subset emitted for the two known Qt fields. Unknown QVariant encodings
// are rejected for review, never guessed or overwritten.
Map<String, Object?> parseQtIni(String source) {
  if (source.length > 2 * 1024 * 1024) {
    throw const FormatException('Qt settings size budget');
  }
  var section = '';
  final result = <String, Object?>{};
  for (final raw in const LineSplitter().convert(source)) {
    final line = raw.trim();
    if (line.startsWith('[') && line.endsWith(']')) {
      section = line.substring(1, line.length - 1);
      continue;
    }
    final at = line.indexOf('=');
    if (at < 0) continue;
    final key = '$section/${line.substring(0, at).trim()}';
    final text = line.substring(at + 1).trim();
    if (key == 'appearance/theme') {
      result['theme'] = int.tryParse(text) ?? text;
    } else if (key == 'archives/recent') {
      if (text == '@Invalid()') {
        result['recent'] = <String>[];
        continue;
      }
      final list = <String>[];
      final word = StringBuffer();
      var quote = false,
          escape = false,
          quotedToken = false,
          closedQuote = false;
      void emit() {
        list.add(quotedToken ? word.toString() : word.toString().trim());
        word.clear();
        quotedToken = false;
        closedQuote = false;
      }

      for (final rune in text.runes) {
        final c = String.fromCharCode(rune);
        if (escape) {
          if (!['\\', '"', ',', 'n', 'r', 't'].contains(c)) {
            throw const FormatException('Unsupported Qt escape');
          }
          word.write({'n': '\n', 'r': '\r', 't': '\t'}[c] ?? c);
          escape = false;
        } else if (c == '\\') {
          escape = true;
        } else if (c == '"') {
          if (!quote) {
            if (closedQuote || word.toString().trim().isNotEmpty) {
              throw const FormatException('Unsupported Qt quoted value');
            }
            word.clear();
            quotedToken = true;
          } else {
            closedQuote = true;
          }
          quote = !quote;
        } else if (c == ',' && !quote) {
          emit();
        } else if (closedQuote) {
          if (c.trim().isNotEmpty) {
            throw const FormatException('Unexpected text after Qt quote');
          }
        } else {
          word.write(c);
        }
      }
      if (quote || escape || text.startsWith('@')) {
        throw const FormatException('Unsupported Qt string list');
      }
      if (word.isNotEmpty || list.isNotEmpty || quotedToken) emit();
      result['recent'] = list;
    }
  }
  return result;
}

Map<String, Object?> _windowsLegacy() => using((a) {
  final get = DynamicLibrary.open('advapi32.dll')
      .lookupFunction<
        Int32 Function(
          IntPtr,
          Pointer<Utf16>,
          Pointer<Utf16>,
          Uint32,
          Pointer<Uint32>,
          Pointer<Void>,
          Pointer<Uint32>,
        ),
        int Function(
          int,
          Pointer<Utf16>,
          Pointer<Utf16>,
          int,
          Pointer<Uint32>,
          Pointer<Void>,
          Pointer<Uint32>,
        )
      >('RegGetValueW');
  final result = <String, Object?>{};
  for (final name in ['theme', 'recent']) {
    final type = a<Uint32>(), size = a<Uint32>();
    final subkey =
        'Software\\AnAnArchiver\\AnAnArchiver\\${name == 'theme' ? 'appearance' : 'archives'}'
            .toNativeUtf16(allocator: a);
    final key = name.toNativeUtf16(allocator: a);
    var status = get(-2147483647, subkey, key, 0xffff, type, nullptr, size);
    if (status == 2) continue;
    if (status != 0 || size.value > 2 * 1024 * 1024) {
      throw const FormatException('Cannot read Qt registry preferences');
    }
    final buffer = a<Uint8>(size.value + 4);
    status = get(-2147483647, subkey, key, 0xffff, type, buffer.cast(), size);
    if (status != 0) {
      throw const FormatException('Qt registry changed while reading');
    }
    if (name == 'theme' && type.value == 4 && size.value == 4) {
      result[name] = buffer.cast<Uint32>().value;
    } else if (name == 'recent' && (type.value == 7 || type.value == 1)) {
      final units = buffer.cast<Uint16>().asTypedList(size.value ~/ 2);
      final text = String.fromCharCodes(units);
      result[name] = text.split('\u0000').where((s) => s.isNotEmpty).toList();
    } else {
      throw const FormatException('Unknown Qt registry value format');
    }
  }
  return result;
});
