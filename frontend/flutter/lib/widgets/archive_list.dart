import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:intl/intl.dart';

import '../controllers/browser_controller.dart';
import '../l10n/app_localizations.dart';
import 'app_icon.dart';

class ArchiveList extends StatefulWidget {
  const ArchiveList(this.controller, {super.key});
  final BrowserController controller;
  @override
  State<ArchiveList> createState() => _ArchiveListState();
}

class _ArchiveListState extends State<ArchiveList> {
  final scroll = ScrollController();
  final listFocus = FocusNode();
  int generation = -1, focusRow = 0;
  double rowHeight = 34.0;
  @override
  void initState() {
    super.initState();
    scroll.addListener(_scroll);
    widget.controller.addListener(_changed);
  }

  void _scroll() => unawaited(
    widget.controller.ensureWindow((scroll.offset / rowHeight).floor()),
  );
  void _changed() {
    if (generation != widget.controller.generation) {
      generation = widget.controller.generation;
      focusRow = 0;
      if (scroll.hasClients) scroll.jumpTo(0);
    }
  }

  @override
  void dispose() {
    widget.controller.removeListener(_changed);
    scroll.dispose();
    listFocus.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    rowHeight =
        34 * (MediaQuery.textScalerOf(context).scale(14) / 14).clamp(1.0, 2.0);
    final c = widget.controller;
    final l = AppLocalizations.of(context)!;
    final colors = Theme.of(context).colorScheme;
    final locale = Localizations.localeOf(context).toLanguageTag();
    return LayoutBuilder(
      builder: (context, space) {
        final compact = space.maxWidth < 650;
        return Column(
          children: [
            SizedBox(
              height: 38,
              child: Row(
                children: [
                  Expanded(flex: 5, child: _header(l.name, 0)),
                  Expanded(flex: 2, child: _header(l.size, 1)),
                  if (!compact) Expanded(flex: 2, child: _header(l.type, 2)),
                  Expanded(flex: 3, child: _header(l.modified, 3)),
                ],
              ),
            ),
            Divider(height: 1, color: colors.outlineVariant),
            Expanded(
              child: c.total == 0
                  ? Center(child: Text(c.busy ? l.loading : l.empty))
                  : Focus(
                      focusNode: listFocus,
                      onFocusChange: (_) {
                        if (mounted) setState(() {});
                      },
                      onKeyEvent: (node, event) {
                        if (event is! KeyDownEvent) {
                          return KeyEventResult.ignored;
                        }
                        if (event.logicalKey == LogicalKeyboardKey.enter) {
                          final row = c.rowAt(focusRow);
                          if (row?.folder == true) {
                            unawaited(c.navigate(row!.path));
                          }
                          return KeyEventResult.handled;
                        }
                        final delta =
                            event.logicalKey == LogicalKeyboardKey.arrowDown
                            ? 1
                            : event.logicalKey == LogicalKeyboardKey.arrowUp
                            ? -1
                            : 0;
                        if (delta == 0) return KeyEventResult.ignored;
                        focusRow = (focusRow + delta).clamp(0, c.total - 1);
                        final offset = (focusRow * rowHeight).clamp(
                          0.0,
                          scroll.position.maxScrollExtent,
                        );
                        scroll.jumpTo(offset);
                        c.choose(
                          focusRow,
                          extend: HardwareKeyboard.instance.isShiftPressed,
                        );
                        return KeyEventResult.handled;
                      },
                      child: DecoratedBox(
                        decoration: BoxDecoration(
                          border: Border.all(
                            color: listFocus.hasFocus
                                ? colors.primary
                                : Colors.transparent,
                          ),
                        ),
                        child: Scrollbar(
                          controller: scroll,
                          thumbVisibility: true,
                          child: ListView.builder(
                            key: ValueKey(c.query),
                            controller: scroll,
                            itemExtent: rowHeight,
                            itemCount: c.total,
                            scrollCacheExtent: const ScrollCacheExtent.pixels(
                              200,
                            ),
                            itemBuilder: (context, index) {
                              final row = c.rowAt(index);
                              if (row == null) {
                                return Padding(
                                  padding: const EdgeInsets.symmetric(
                                    horizontal: 16,
                                  ),
                                  child: Align(
                                    alignment: Alignment.centerLeft,
                                    child: Text(
                                      l.loading,
                                      style: TextStyle(color: colors.outline),
                                    ),
                                  ),
                                );
                              }
                              final selected = c.selection.contains(index);
                              final size = row.folder || row.flags & 1 == 0
                                  ? '—'
                                  : '${NumberFormat.compact(locale: locale).format(row.size)} B';
                              final date = row.flags & 4 == 0
                                  ? '—'
                                  : DateFormat.yMd(locale).add_Hm().format(
                                      DateTime.fromMillisecondsSinceEpoch(
                                        row.modified,
                                      ),
                                    );
                              final type = row.folder
                                  ? l.folder
                                  : row.type == 1
                                  ? l.file
                                  : row.type == 3 || row.type == 4
                                  ? l.link
                                  : l.other;
                              return Semantics(
                                selected: selected,
                                label: row.name,
                                child: Ink(
                                  color: selected
                                      ? colors.secondaryContainer
                                      : index.isOdd
                                      ? colors.surfaceContainerLowest
                                      : colors.surface,
                                  child: InkWell(
                                    key: ValueKey(
                                      '${c.archive}:${row.identity}',
                                    ),
                                    onTap: () {
                                      listFocus.requestFocus();
                                      focusRow = index;
                                      c.choose(
                                        index,
                                        additive:
                                            HardwareKeyboard
                                                .instance
                                                .isControlPressed ||
                                            HardwareKeyboard
                                                .instance
                                                .isMetaPressed,
                                        extend: HardwareKeyboard
                                            .instance
                                            .isShiftPressed,
                                      );
                                    },
                                    onDoubleTap: row.folder
                                        ? () => c.navigate(row.path)
                                        : null,
                                    child: Row(
                                      children: [
                                        Expanded(
                                          flex: 5,
                                          child: Padding(
                                            padding: const EdgeInsets.symmetric(
                                              horizontal: 12,
                                            ),
                                            child: Row(
                                              children: [
                                                AppIcon(
                                                  row.folder
                                                      ? 'folder'
                                                      : 'file',
                                                  size: 18,
                                                ),
                                                const SizedBox(width: 9),
                                                Expanded(
                                                  child: Text(
                                                    row.name,
                                                    maxLines: 1,
                                                    overflow:
                                                        TextOverflow.ellipsis,
                                                  ),
                                                ),
                                              ],
                                            ),
                                          ),
                                        ),
                                        Expanded(
                                          flex: 2,
                                          child: Text(
                                            size,
                                            maxLines: 1,
                                            overflow: TextOverflow.ellipsis,
                                          ),
                                        ),
                                        if (!compact)
                                          Expanded(
                                            flex: 2,
                                            child: Text(
                                              type,
                                              maxLines: 1,
                                              overflow: TextOverflow.ellipsis,
                                            ),
                                          ),
                                        Expanded(
                                          flex: 3,
                                          child: Text(
                                            date,
                                            maxLines: 1,
                                            overflow: TextOverflow.ellipsis,
                                          ),
                                        ),
                                      ],
                                    ),
                                  ),
                                ),
                              );
                            },
                          ),
                        ),
                      ),
                    ),
            ),
          ],
        );
      },
    );
  }

  Widget _header(String label, int column) {
    final c = widget.controller;
    return TextButton(
      style: TextButton.styleFrom(
        alignment: Alignment.centerLeft,
        padding: const EdgeInsets.symmetric(horizontal: 12),
      ),
      onPressed: () => c.setSort(column),
      child: Text(
        '$label${c.sort == column ? (c.descending ? ' ↓' : ' ↑') : ''}',
        maxLines: 1,
      ),
    );
  }
}
