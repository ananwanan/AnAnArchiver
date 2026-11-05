import 'dart:convert';
import 'dart:io';
import 'dart:isolate';

Future<Map<String, dynamic>> loadBuildRecords() => Isolate.run(() {
  final executable = File(Platform.resolvedExecutable).parent;
  final dir = Platform.isMacOS
      ? Directory('${executable.path}/../Frameworks/litearc-build')
      : Platform.isLinux
      ? Directory('${executable.path}/lib/litearc-build')
      : Directory('${executable.path}/litearc-build');
  Map<String, dynamic> read(String name) =>
      jsonDecode(File('${dir.path}/$name').readAsStringSync())
          as Map<String, dynamic>;
  return {
    'native': read('dependency-provenance.json'),
    'flutter': read('flutter-sdk.json'),
  };
});
