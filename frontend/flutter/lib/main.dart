import 'dart:async';
import 'dart:ui' show AppExitResponse;

import 'package:flutter/material.dart';
import 'package:litearc_binding/litearc_binding.dart';

import 'controllers/browser_controller.dart';
import 'l10n/app_localizations.dart';
import 'pages/browser_page.dart';
import 'platform/desktop_adapter.dart';

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(LiteArcApp(initialPath: args.isEmpty ? null : args.first));
}

class LiteArcApp extends StatefulWidget {
  const LiteArcApp({
    super.key,
    this.controller,
    this.platform,
    this.initialPath,
    this.initialLocale,
  });
  final BrowserController? controller;
  final DesktopAdapter? platform;
  final String? initialPath;
  final Locale? initialLocale;
  @override
  State<LiteArcApp> createState() => _LiteArcAppState();
}

class _LiteArcAppState extends State<LiteArcApp> {
  BrowserController? controller;
  Object? startupError;
  Locale? locale;
  ThemeMode theme = ThemeMode.system;
  late final AppLifecycleListener lifecycle;
  @override
  void initState() {
    super.initState();
    controller = widget.controller;
    locale = widget.initialLocale;
    lifecycle = AppLifecycleListener(
      onExitRequested: () async {
        await controller?.shutdown();
        return AppExitResponse.exit;
      },
    );
    if (controller == null) unawaited(_start());
  }

  Future<void> _start() async {
    try {
      final binding = await BrowserClient.start();
      if (!mounted) {
        await binding.close();
        return;
      }
      setState(() => controller = BrowserController(binding));
      if (widget.initialPath case final path?) {
        unawaited(controller!.open(path));
      }
    } catch (e) {
      if (mounted) setState(() => startupError = e);
    }
  }

  @override
  void dispose() {
    lifecycle.dispose();
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
            platform: widget.platform ?? SystemDesktopAdapter(),
            onLocale: (value) => setState(() => locale = value),
            onTheme: (value) => setState(() => theme = value),
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
