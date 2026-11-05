import 'dart:io';
import 'dart:convert';

import 'package:crypto/crypto.dart';

import 'package:ffigen/ffigen.dart';

Future<void> main(List<String> args) async {
  final package = Platform.script.resolve('../');
  final header = package.resolve(
    '../../../../include/ananarchiver/c_api/ananarchiver.h',
  );
  final generator = FfiGenerator(
    input: Input(entryPoints: [header], include: (uri) => uri == header),
    output: Output(
      dart: DartOutput(path: package.resolve('lib/src/generated.dart')),
      style: const DynamicLibraryBindings(wrapperName: 'LiteArcNative'),
      commentType: const CommentType.none(),
    ),
    visitors: [
      Visitor(
        func: (n) => n.isIncluded = n.originalName.startsWith('aa_'),
        struct: (n) => n.isIncluded = n.originalName.startsWith('aa_'),
        macroConstant: (n) => n.isIncluded = n.originalName.startsWith('AA_'),
      ),
    ],
  );
  await generator.generate(
    libclangDylib: args.isEmpty ? null : File(args.single).absolute.uri,
  );
  final record = {
    'header': 'include/ananarchiver/c_api/ananarchiver.h',
    'sha256': sha256
        .convert(await File.fromUri(header).readAsBytes())
        .toString(),
    'generator': 'ffigen (version recorded in package pubspec.lock)',
    'dart': Platform.version,
    'abi': 1,
    'scope':
        'All aa_* functions/structs and AA_* constants; native C alignment',
  };
  await File.fromUri(package.resolve('binding-source.json'))
      .writeAsString('${const JsonEncoder.withIndent('  ').convert(record)}\n');
}
