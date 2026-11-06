import 'dart:async';
import 'dart:ui' show AppExitResponse;

import 'package:flutter/material.dart';
import 'package:litearc_binding/litearc_binding.dart';

import 'controllers/browser_controller.dart';
import 'l10n/app_localizations.dart';
import 'pages/browser_page.dart';
import 'platform/desktop_adapter.dart';
import 'platform/settings_store.dart';
import 'controllers/settings_controller.dart';
import 'controllers/desktop_controller.dart';

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(LiteArcApp(initialFiles: args));
}

class LiteArcApp extends StatefulWidget {
  const LiteArcApp({
    super.key,
    this.controller,
    this.platform,
    this.initialPath,
    this.initialLocale,
    this.initialFiles = const [],
    this.settings,
  });
  final BrowserController? controller;
  final DesktopAdapter? platform;
  final String? initialPath;
  final Locale? initialLocale;
  final List<String> initialFiles;
  final SettingsController? settings;
  @override
  State<LiteArcApp> createState() => _LiteArcAppState();
}

class _LiteArcAppState extends State<LiteArcApp> {
  BrowserController? controller;
  Object? startupError;
  Locale? locale;
  ThemeMode theme = ThemeMode.system;
  SettingsController? settings;
  late final DesktopAdapter platform;
  DesktopOpenController? external;
  StreamSubscription<List<String>>? openSubscription;
  late final AppLifecycleListener lifecycle;
  @override
  void initState() {
    super.initState();
    controller = widget.controller;
    platform = widget.platform ?? SystemDesktopAdapter();
    settings = widget.settings;
    locale = widget.initialLocale;
    lifecycle = AppLifecycleListener(
      onExitRequested: () async {
        external?.close();
        await openSubscription?.cancel();
        if (platform is SystemDesktopAdapter) {
          await (platform as SystemDesktopAdapter).dispose();
        }
        if (platform case final DesktopEvents events) {
          try {
            final state = await events.windowState();
            settings?.update(
              width: state['width'] as int?,
              height: state['height'] as int?,
              maximized: state['maximized'] as bool?,
            );
          } catch (e) {
            settings?.error = e;
          }
        }
        final nativeShutdown = controller?.shutdown();
        await settings?.flush();
        await nativeShutdown;
        return AppExitResponse.exit;
      },
    );
    if (controller == null) {
      unawaited(_start());
    } else {
      _connect();
    }
  }

  Future<void> _start() async {
    try {
      settings ??= SettingsController(SettingsStore.system());
      await settings?.load();
      if (!mounted) return;
      final prefs = settings?.value;
      if (prefs != null) {
        theme = ThemeMode.values[prefs.theme];
        locale = prefs.language == 'system'
            ? null
            : prefs.language == 'en'
            ? const Locale('en')
            : const Locale('zh', 'CN');
        if (platform case final DesktopEvents events) {
          await events.restoreWindow(prefs.toJson());
        }
      }
      final binding = await BrowserClient.start();
      if (!mounted) {
        await binding.close();
        return;
      }
      setState(() => controller = BrowserController(binding));
      _connect();
    } catch (e) {
      if (mounted) setState(() => startupError = e);
    }
  }

  void _connect() {
    final c = controller!;
    c.onOpened = settings?.remember;
    external = DesktopOpenController(c);
    final files = widget.initialPath == null
        ? widget.initialFiles
        : [widget.initialPath!];
    if (files.isNotEmpty) external!.openFiles(files);
    if (platform case final DesktopEvents events) {
      openSubscription = events.openFiles.listen(external!.openFiles);
      unawaited(
        events.ready().then((files) {
          if (mounted && files.isNotEmpty) external?.openFiles(files);
        }),
      );
    }
  }

  @override
  void dispose() {
    lifecycle.dispose();
    external?.close();
    unawaited(openSubscription?.cancel());
    if (platform case final DesktopEvents events) {
      unawaited(events.dispose());
    }
    settings?.dispose();
    controller?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => MaterialApp(
    debugShowCheckedModeBanner: false,
    locale: locale,
    themeMode: theme,
    localizationsDelegates: AppLocalizations.localizationsDelegates,
    supportedLocales: AppLocalizations.supportedLocales,
    onGenerateTitle: (context) => AppLocalizations.of(context)!.appTitle,
    theme: _theme(Brightness.light),
    darkTheme: _theme(Brightness.dark),
    home: Builder(
      builder: (context) {
        if (controller case final c?) {
          return BrowserPageView(
            controller: c,
            platform: platform,
            settings: settings,
            onLocale: (value) {
              settings?.update(
                language: value == null
                    ? 'system'
                    : value.languageCode == 'en'
                    ? 'en'
                    : 'zh-CN',
              );
              setState(() => locale = value);
            },
            onTheme: (value) {
              settings?.update(theme: value.index);
              setState(() => theme = value);
            },
          );
        }
        final l = AppLocalizations.of(context)!;
        return Scaffold(
          body: Center(
            child: startupError == null
                ? const CircularProgressIndicator()
                : Padding(
                    padding: const EdgeInsets.all(24),
                    child: SelectableText('${l.errorTitle}\n$startupError'),
                  ),
          ),
        );
      },
    ),
  );
  ThemeData _theme(Brightness brightness) => ThemeData(
    brightness: brightness,
    colorScheme: ColorScheme.fromSeed(
      seedColor: const Color(0xff2467b2),
      brightness: brightness,
    ),
    useMaterial3: true,
    visualDensity: VisualDensity.compact,
    inputDecorationTheme: const InputDecorationTheme(
      border: OutlineInputBorder(),
    ),
  );
}
