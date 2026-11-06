import 'dart:async';

import 'package:flutter/material.dart';
import 'package:litearc_binding/litearc_binding.dart';
import 'package:flutter/services.dart';

import '../controllers/browser_controller.dart';
import '../l10n/app_localizations.dart';
import '../platform/desktop_adapter.dart';
import '../widgets/app_icon.dart';
import '../widgets/build_details.dart';
import '../widgets/archive_list.dart';
import '../widgets/folder_tree.dart';
import '../widgets/operation_panel.dart';
import '../widgets/create_archive_dialog.dart';
import '../controllers/settings_controller.dart';
import '../controllers/desktop_controller.dart';
import '../widgets/task_center.dart';
import '../widgets/desktop_transfer.dart';

import 'package:path/path.dart' as path;

class BrowserPageView extends StatefulWidget {
  const BrowserPageView({
    super.key,
    required this.controller,
    required this.platform,
    required this.onLocale,
    required this.onTheme,
    this.settings,
  });
  final BrowserController controller;
  final DesktopAdapter platform;
  final ValueChanged<Locale?> onLocale;
  final ValueChanged<ThemeMode> onTheme;
  final SettingsController? settings;
  @override
  State<BrowserPageView> createState() => _BrowserPageViewState();
}

class _BrowserPageViewState extends State<BrowserPageView> {
  ExportController? export;
  bool dropping = false;
  @override
  void initState() {
    super.initState();
    if (widget.settings case final settings?) {
      export = ExportController(
        widget.controller,
        path.join(settings.store.directory, 'exports'),
      );
    }
  }

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
  final searchFocus = FocusNode();
  Future<void> extract(bool all) async {
    final c = widget.controller;
    final generation = c.generation;
    final l = AppLocalizations.of(context)!;
    try {
      final destination = await widget.platform.chooseDirectory();
      if (!mounted || destination == null) return;
      if (generation != c.generation) {
        throw const BrowserFailure(
          9,
          5,
          'Archive view changed while choosing destination',
        );
      }
      var policy = 0;
      final selected = await showDialog<int>(
        context: context,
        builder: (context) => StatefulBuilder(
          builder: (context, set) => AlertDialog(
            title: Text(all ? l.extractAll : l.extractSelected),
            content: SizedBox(
              width: 420,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(destination),
                  const SizedBox(height: 12),
                  DropdownButtonFormField<int>(
                    initialValue: policy,
                    isExpanded: true,
                    decoration: InputDecoration(labelText: l.overwrite),
                    items: [
                      for (var i = 0; i <= 4; i++)
                        DropdownMenuItem(
                          value: i,
                          child: Text(l.overwritePolicy('p$i')),
                        ),
                    ],
                    onChanged: (v) => set(() => policy = v!),
                  ),
                ],
              ),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(context),
                child: Text(l.cancel),
              ),
              FilledButton(
                onPressed: () => Navigator.pop(context, policy),
                child: Text(l.extract),
              ),
            ],
          ),
        ),
      );
      if (!mounted || selected == null) return;
      if (generation != c.generation) {
        throw const BrowserFailure(
          9,
          5,
          'Archive view changed while choosing options',
        );
      }
      await c.extract(destination, all: all, overwrite: selected);
    } catch (e) {
      if (mounted) c.operations.reportFailure(e);
    }
  }

  Future<void> create([List<String> inputs = const []]) async {
    final c = widget.controller;
    try {
      final capabilities = (await c.transport.request('capabilities'))
          .capabilities;
      if (!mounted) return;
      if (capabilities.isEmpty) {
        throw const BrowserFailure(8, 5, 'No native creation capabilities');
      }
      final options = await showDialog<Map<String, Object?>>(
        context: context,
        builder: (context) => CreateArchiveDialog(
          capabilities: capabilities,
          platform: widget.platform,
          initialPaths: inputs,
        ),
      );
      if (mounted && options != null) {
        await c.operations.run('compress', options);
      }
    } catch (e) {
      if (mounted) c.operations.reportFailure(e);
    }
  }

  Future<void> drop(List<String> paths) async {
    if (paths.isEmpty || dropping || widget.controller.operations.busy) return;
    setState(() => dropping = true);
    try {
      if (paths.length == 1) {
        final result = await widget.controller.transport.request('probe', {
          'path': paths.single,
        });
        if (!mounted) return;
        if (result.classification == 'archive') {
          await widget.controller.open(paths.single);
          return;
        }
      }
      if (mounted) await create(paths);
    } finally {
      if (mounted) setState(() => dropping = false);
    }
  }

  Future<void> retainedExports() async {
    final e = export;
    if (e == null) return;
    try {
      final result = await e.retained();
      if (!mounted) return;
      final l = AppLocalizations.of(context)!;
      await showDialog<void>(
        context: context,
        builder: (context) => AlertDialog(
          title: Text(l.exportFiles),
          content: SizedBox(
            width: 650,
            child: SingleChildScrollView(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(l.exportRetention),
                  for (final root in result.paths) SelectableText(root),
                ],
              ),
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text(l.close),
            ),
            if (result.paths.isNotEmpty)
              TextButton(
                onPressed: () async {
                  Navigator.pop(context);
                  try {
                    for (final root in result.paths) {
                      await e.release(root);
                    }
                  } catch (error) {
                    widget.controller.operations.reportFailure(error);
                  }
                },
                child: Text(l.releaseExports),
              ),
          ],
        ),
      );
    } catch (e) {
      widget.controller.operations.reportFailure(e);
    }
  }

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
    searchFocus.dispose();
    export?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    final l = AppLocalizations.of(context)!;
    return Focus(
      onKeyEvent: (_, event) {
        if (event is! KeyDownEvent ||
            ModalRoute.of(context)?.isCurrent != true) {
          return KeyEventResult.ignored;
        }
        final focus = FocusManager.instance.primaryFocus?.context;
        if (focus?.widget is EditableText ||
            focus?.findAncestorWidgetOfExactType<EditableText>() != null) {
          return KeyEventResult.ignored;
        }
        final keyboard = HardwareKeyboard.instance;
        final mac = Theme.of(context).platform == TargetPlatform.macOS;
        final primary = mac
            ? keyboard.isMetaPressed
            : keyboard.isControlPressed;
        final key = event.logicalKey;
        if (primary && key == LogicalKeyboardKey.keyO) {
          unawaited(open());
        } else if (primary &&
            key == LogicalKeyboardKey.keyN &&
            !c.operations.busy) {
          unawaited(create());
        } else if (primary &&
            key == LogicalKeyboardKey.keyE &&
            c.archive != 0 &&
            !c.busy &&
            !c.operations.busy) {
          unawaited(extract(keyboard.isShiftPressed || !c.canExtractSelected));
        } else if (primary && key == LogicalKeyboardKey.keyA) {
          c.selectAll();
        } else if (primary && key == LogicalKeyboardKey.keyF && !c.isHome) {
          searchFocus.requestFocus();
        } else if (((!mac && keyboard.isAltPressed) || (mac && primary)) &&
            key == LogicalKeyboardKey.arrowUp &&
            !c.isHome) {
          unawaited(mac && keyboard.isShiftPressed ? c.navigate('') : c.up());
        } else if (!mac &&
            keyboard.isAltPressed &&
            key == LogicalKeyboardKey.home &&
            !c.isHome) {
          unawaited(c.navigate(''));
        } else if (primary &&
            key == LogicalKeyboardKey.period &&
            c.operations.busy) {
          unawaited(c.operations.cancel());
        } else if (key == LogicalKeyboardKey.escape && c.busy) {
          unawaited(c.cancelBrowsing());
        } else {
          return KeyEventResult.ignored;
        }
        return KeyEventResult.handled;
      },
      child: Focus(
        autofocus: true,
        child: AnimatedBuilder(
          animation: Listenable.merge([
            c,
            c.operations,
            ?widget.settings,
            ?export,
          ]),
          builder: (context, _) {
            if (search.text != c.search) {
              search.value = TextEditingValue(
                text: c.search,
                selection: TextSelection.collapsed(offset: c.search.length),
              );
            }
            final colors = Theme.of(context).colorScheme;
            final content = Scaffold(
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
                            tooltip: l.taskCenter,
                            onPressed: () => showTaskCenter(context, c),
                            icon: const Icon(Icons.list_alt, size: 21),
                          ),
                          if (export != null)
                            IconButton(
                              tooltip: l.exportFiles,
                              onPressed: retainedExports,
                              icon: const Icon(
                                Icons.folder_copy_outlined,
                                size: 21,
                              ),
                            ),
                          IconButton(
                            tooltip: l.components,
                            onPressed: () => showBuildDetails(context),
                            icon: const Icon(Icons.info_outline, size: 21),
                          ),
                          PopupMenuButton<Locale>(
                            tooltip: l.language,
                            onSelected: (v) => widget.onLocale(
                              v.languageCode == 'system' ? null : v,
                            ),
                            icon: const Icon(Icons.language, size: 21),
                            itemBuilder: (_) => [
                              PopupMenuItem(
                                value: const Locale('system'),
                                child: Text(l.system),
                              ),
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
                    Padding(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 12,
                        vertical: 4,
                      ),
                      child: Wrap(
                        spacing: 8,
                        children: [
                          if (export case final e?) ...[
                            TextButton.icon(
                              onPressed:
                                  c.canExtractSelected &&
                                      !c.operations.busy &&
                                      !e.preparing
                                  ? e.prepare
                                  : null,
                              icon: const Icon(
                                Icons.drive_folder_upload_outlined,
                                size: 20,
                              ),
                              label: Text(l.prepareDrag),
                            ),
                            if (e.paths.isNotEmpty) ExportDragHandle(e),
                          ],
                          TextButton.icon(
                            onPressed:
                                c.operations.busy ||
                                    c.operations.waitingForPassword
                                ? null
                                : create,
                            icon: const Icon(Icons.archive_outlined, size: 20),
                            label: Text(l.createArchive),
                          ),
                          TextButton.icon(
                            onPressed:
                                c.archive == 0 ||
                                    c.busy ||
                                    c.operations.busy ||
                                    c.operations.waitingForPassword
                                ? null
                                : () => extract(true),
                            icon: const Icon(
                              Icons.unarchive_outlined,
                              size: 20,
                            ),
                            label: Text(l.extractAll),
                          ),
                          TextButton(
                            onPressed:
                                !c.canExtractSelected ||
                                    c.operations.busy ||
                                    c.operations.waitingForPassword
                                ? null
                                : () => extract(false),
                            child: Text(l.extractSelected),
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
                                focusNode: searchFocus,
                                key: const Key('archive-search'),
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
                    if (widget.settings?.error != null)
                      MaterialBanner(
                        content: Text(l.settingsProblem),
                        actions: [
                          TextButton(
                            onPressed: () => showDialog<void>(
                              context: context,
                              builder: (_) => AlertDialog(
                                title: Text(l.details),
                                content: SelectableText(
                                  '${widget.settings!.error}',
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
                    Expanded(
                      child: c.isHome
                          ? Center(
                              child: SingleChildScrollView(
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
                                    if (widget.settings
                                        case final settings?) ...[
                                      const SizedBox(height: 12),
                                      Text(l.recentArchives),
                                      TextButton(
                                        onPressed: settings.value.recent.isEmpty
                                            ? null
                                            : settings.clear,
                                        child: Text(l.clearHistory),
                                      ),
                                      ConstrainedBox(
                                        constraints: const BoxConstraints(
                                          maxHeight: 200,
                                          maxWidth: 650,
                                        ),
                                        child: ListView(
                                          shrinkWrap: true,
                                          children: [
                                            for (final recent
                                                in settings.value.recent)
                                              ListTile(
                                                dense: true,
                                                title: Text(
                                                  path.basename(recent),
                                                ),
                                                subtitle: Text(
                                                  recent,
                                                  maxLines: 1,
                                                  overflow:
                                                      TextOverflow.ellipsis,
                                                ),
                                                onTap: () => c.open(recent),
                                                trailing: IconButton(
                                                  tooltip: l.removeRecent,
                                                  onPressed: () =>
                                                      settings.remove(recent),
                                                  icon: const Icon(
                                                    Icons.close,
                                                    size: 18,
                                                  ),
                                                ),
                                              ),
                                          ],
                                        ),
                                      ),
                                    ],
                                  ],
                                ),
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
                    ConstrainedBox(
                      constraints: BoxConstraints(
                        maxHeight: MediaQuery.sizeOf(context).height * 0.45,
                      ),
                      child: SingleChildScrollView(
                        child: OperationPanel(c.operations),
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
                              onPressed: c.cancelBrowsing,
                              child: Text(l.cancel),
                            ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            );
            return widget.platform is DesktopEvents
                ? SystemFileDrop(
                    enabled:
                        !dropping &&
                        !c.operations.busy &&
                        !c.operations.waitingForPassword,
                    onFiles: drop,
                    onError: c.operations.reportFailure,
                    child: content,
                  )
                : content;
          },
        ),
      ),
    );
  }
}
