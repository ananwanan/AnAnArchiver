import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../controllers/browser_controller.dart';
import '../l10n/app_localizations.dart';
import '../platform/desktop_adapter.dart';
import '../widgets/app_icon.dart';
import '../widgets/build_details.dart';
import '../widgets/archive_list.dart';
import '../widgets/folder_tree.dart';

class BrowserPageView extends StatefulWidget {
  const BrowserPageView({
    super.key,
    required this.controller,
    required this.platform,
    required this.onLocale,
    required this.onTheme,
  });
  final BrowserController controller;
  final DesktopAdapter platform;
  final ValueChanged<Locale> onLocale;
  final ValueChanged<ThemeMode> onTheme;
  @override
  State<BrowserPageView> createState() => _BrowserPageViewState();
}

class _BrowserPageViewState extends State<BrowserPageView> {
  String? windowTitle;
  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final title = AppLocalizations.of(context)!.appTitle;
    if (windowTitle != title) {
      windowTitle = title;
      unawaited(widget.platform.setTitle(title));
    }
  }

  final search = TextEditingController();
  Future<void> open() async {
    try {
      final path = await widget.platform.chooseArchive();
      if (mounted && path != null) await widget.controller.open(path);
    } catch (e) {
      if (mounted) {
        final l = AppLocalizations.of(context)!;
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('${l.errorTitle}: $e')));
      }
    }
  }

  @override
  void dispose() {
    search.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    final l = AppLocalizations.of(context)!;
    return CallbackShortcuts(
      bindings: {
        const SingleActivator(LogicalKeyboardKey.keyO, control: true): () =>
            unawaited(open()),
        const SingleActivator(LogicalKeyboardKey.keyA, control: true):
            c.selectAll,
        const SingleActivator(LogicalKeyboardKey.keyO, meta: true): () =>
            unawaited(open()),
        const SingleActivator(LogicalKeyboardKey.keyA, meta: true): c.selectAll,
        const SingleActivator(LogicalKeyboardKey.arrowUp, alt: true): () =>
            unawaited(c.up()),
        const SingleActivator(LogicalKeyboardKey.escape): () =>
            unawaited(c.cancel()),
      },
      child: Focus(
        autofocus: true,
        child: AnimatedBuilder(
          animation: c,
          builder: (context, _) {
            if (search.text != c.search) {
              search.value = TextEditingValue(
                text: c.search,
                selection: TextSelection.collapsed(offset: c.search.length),
              );
            }
            final colors = Theme.of(context).colorScheme;
            return Scaffold(
              body: SafeArea(
                child: Column(
                  children: [
                    Container(
                      height: 64,
                      padding: const EdgeInsets.symmetric(horizontal: 18),
                      color: colors.surfaceContainerLow,
                      child: Row(
                        children: [
                          Image.asset(
                            'assets/branding/ananarchiver.png',
                            width: 34,
                            height: 34,
                          ),
                          const SizedBox(width: 10),
                          Text(
                            l.appTitle,
                            style: Theme.of(context).textTheme.titleLarge,
                          ),
                          const SizedBox(width: 28),
                          FilledButton.tonalIcon(
                            onPressed: open,
                            icon: const AppIcon('open'),
                            label: Text(l.openArchive),
                          ),
                          if (!c.isHome)
                            IconButton(
                              tooltip: l.closeArchive,
                              onPressed: c.closeArchive,
                              icon: const AppIcon('close'),
                            ),
                          const Spacer(),
                          IconButton(
                            tooltip: l.components,
                            onPressed: () => showBuildDetails(context),
                            icon: const Icon(Icons.info_outline, size: 21),
                          ),
                          PopupMenuButton<Locale>(
                            tooltip: l.language,
                            onSelected: widget.onLocale,
                            icon: const Icon(Icons.language, size: 21),
                            itemBuilder: (_) => [
                              const PopupMenuItem(
                                value: Locale('zh', 'CN'),
                                child: Text('简体中文'),
                              ),
                              const PopupMenuItem(
                                value: Locale('en'),
                                child: Text('English'),
                              ),
                            ],
                          ),
                          PopupMenuButton<ThemeMode>(
                            tooltip: l.theme,
                            onSelected: widget.onTheme,
                            icon: const Icon(
                              Icons.brightness_6_outlined,
                              size: 21,
                            ),
                            itemBuilder: (_) => [
                              PopupMenuItem(
                                value: ThemeMode.system,
                                child: Text(l.system),
                              ),
                              PopupMenuItem(
                                value: ThemeMode.light,
                                child: Text(l.light),
                              ),
                              PopupMenuItem(
                                value: ThemeMode.dark,
                                child: Text(l.dark),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),
                    if (!c.isHome)
                      Padding(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: 8,
                        ),
                        child: Row(
                          children: [
                            IconButton(
                              tooltip: l.archiveRoot,
                              onPressed: () => c.navigate(''),
                              icon: const AppIcon('home'),
                            ),
                            IconButton(
                              tooltip: l.up,
                              onPressed: c.folder.isEmpty ? null : c.up,
                              icon: const Icon(Icons.arrow_upward, size: 19),
                            ),
                            Expanded(
                              child: SelectableText(
                                '${c.path?.split(RegExp(r'[/\\]')).last} / ${c.folder}',
                                maxLines: 1,
                              ),
                            ),
                            const SizedBox(width: 16),
                            SizedBox(
                              width: 220,
                              child: TextField(
                                controller: search,
                                onChanged: c.setSearch,
                                decoration: InputDecoration(
                                  isDense: true,
                                  hintText: l.search,
                                  prefixIcon: const Padding(
                                    padding: EdgeInsets.all(12),
                                    child: AppIcon('search', size: 16),
                                  ),
                                  border: const OutlineInputBorder(),
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    if (c.error != null)
                      Material(
                        color: colors.errorContainer,
                        child: Padding(
                          padding: const EdgeInsets.all(10),
                          child: Row(
                            children: [
                              const AppIcon('warning'),
                              const SizedBox(width: 10),
                              Expanded(
                                child: Text(
                                  l.errorMessage('e${c.error!.code}'),
                                ),
                              ),
                              TextButton(
                                onPressed: () => showDialog<void>(
                                  context: context,
                                  builder: (context) => AlertDialog(
                                    title: Text(l.errorTitle),
                                    content: SizedBox(
                                      width: 600,
                                      child: SingleChildScrollView(
                                        child: SelectableText(
                                          '${l.errorMessage('e${c.error!.code}')}\n\n${c.error!.message}\n${c.error!.backend}\n${c.error!.path}\n'
                                          'AA status: ${c.error!.status}; code: ${c.error!.code}',
                                        ),
                                      ),
                                    ),
                                    actions: [
                                      TextButton(
                                        onPressed: () => Navigator.pop(context),
                                        child: Text(l.close),
                                      ),
                                    ],
                                  ),
                                ),
                                child: Text(l.details),
                              ),
                            ],
                          ),
                        ),
                      ),
                    if (c.busy) const LinearProgressIndicator(minHeight: 2),
                    Expanded(
                      child: c.isHome
                          ? Center(
                              child: Column(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  Image.asset(
                                    'assets/branding/ananarchiver.png',
                                    width: 96,
                                    height: 96,
                                  ),
                                  const SizedBox(height: 24),
                                  Text(
                                    l.homeTitle,
                                    style: Theme.of(context)
                                        .textTheme
                                        .headlineSmall,
                                  ),
                                  const SizedBox(height: 10),
                                  Text(l.homeBody),
                                  const SizedBox(height: 24),
                                  FilledButton.icon(
                                    onPressed: open,
                                    icon: const AppIcon('open'),
                                    label: Text(l.openArchive),
                                  ),
                                ],
                              ),
                            )
                          : Row(
                              children: [
                                SizedBox(width: 220, child: FolderTree(c)),
                                VerticalDivider(
                                  width: 1,
                                  color: colors.outlineVariant,
                                ),
                                Expanded(child: ArchiveList(c)),
                              ],
                            ),
                    ),
                    Container(
                      height: 36,
                      padding: const EdgeInsets.symmetric(horizontal: 16),
                      color: colors.surfaceContainerLow,
                      child: Row(
                        children: [
                          Text(
                            c.isHome
                                ? l.home
                                : l.status(c.total, c.selection.count),
                          ),
                          const Spacer(),
                          if (c.progress case final p?)
                            Flexible(
                              child: Text(
                                l.task(
                                  l.operation(
                                    p.operation == 'select'
                                        ? 'selection'
                                        : p.operation,
                                  ),
                                  l.taskState('s${p.state}'),
                                  p.entries,
                                  p.elapsedMs,
                                ),
                                overflow: TextOverflow.ellipsis,
                                maxLines: 1,
                              ),
                            ),
                          if (c.busy)
                            TextButton(
                              onPressed: c.cancel,
                              child: Text(l.cancel),
                            ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            );
          },
        ),
      ),
    );
  }
}
