import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:litearc/widgets/desktop_transfer.dart';
import 'package:super_drag_and_drop/super_drag_and_drop.dart';
import 'package:super_clipboard/super_clipboard.dart'
    show DataReader, ReadProgress;

// Protocol fakes only. Real OLE / Explorer acceptance is recorded separately.
class Progress implements ReadProgress {
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

class Reader implements DataReader {
  @override
  dynamic noSuchMethod(Invocation invocation) {
    if (invocation.memberName == #getValue) {
      (invocation.positionalArguments[1] as Function)(
        Uri.file('${Directory.systemTemp.path}/plain.txt'),
      );
      return Progress();
    }
    return super.noSuchMethod(invocation);
  }
}

class Item extends DropItem {
  @override
  final DataReader dataReader = Reader();
  @override
  Object? get localData => null;
  @override
  bool canProvide(DataFormat format) => true;
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

class Session extends DropSession {
  @override
  final List<DropItem> items = [Item()];
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

void main() {
  testWidgets(
    'native drop acknowledgement never waits for modal/controller completion; later errors are reported',
    (tester) async {
      final modal = Completer<void>();
      final started = Completer<void>();
      final errors = <Object>[];
      late DropRegion region;
      await tester.pumpWidget(
        MaterialApp(
          home: Builder(
            builder: (context) {
              region = SystemFileDrop(
                enabled: true,
                onError: errors.add,
                onFiles: (paths) async {
                  expect(paths.single.endsWith('plain.txt'), true);
                  started.complete();
                  await modal.future;
                },
                child: const SizedBox(),
              ).build(context) as DropRegion;
              return const SizedBox();
            },
          ),
        ),
      );
      final acknowledged = region.onPerformDrop(
        PerformDropEvent(
          session: Session(),
          position: DropPosition(local: Offset.zero, global: Offset.zero),
          acceptedOperation: DropOperation.copy,
        ),
      );
      await acknowledged;
      await started.future;
      expect(
        modal.isCompleted,
        false,
      ); // OS is released while workflow still needs input.
      modal.completeError(StateError('controller failure'));
      await tester.pump();
      expect(errors.single, isA<StateError>());
      await tester.pumpWidget(const SizedBox());
    },
  );
}
