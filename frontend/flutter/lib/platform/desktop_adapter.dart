import 'package:file_selector/file_selector.dart';
import 'package:flutter/services.dart';

abstract interface class DesktopAdapter {
  Future<String?> chooseArchive();
  Future<void> setTitle(String title);
}

class SystemDesktopAdapter implements DesktopAdapter {
  @override
  Future<void> setTitle(String title) =>
      const MethodChannel('litearc/window').invokeMethod<void>('title', title);
  @override
  Future<String?> chooseArchive() async {
    // Deliberately allow all files: format recognition belongs to Application.
    final file = await openFile();
    return file?.path;
  }
}
