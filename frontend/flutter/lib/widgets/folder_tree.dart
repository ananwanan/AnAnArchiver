import 'package:flutter/material.dart';
import 'package:litearc_binding/litearc_binding.dart';

import '../controllers/browser_controller.dart';
import '../l10n/app_localizations.dart';
import 'app_icon.dart';

class FolderTree extends StatelessWidget {
  const FolderTree(this.controller, {super.key});
  final BrowserController controller;
  @override
  Widget build(BuildContext context) {
    final c = controller;
    final l = AppLocalizations.of(context)!;
    final nodes = <({String parent, int depth, BrowserRow? row, int offset})>[];
    void append(String folder, int depth) {
      final page = c.folderWindows[folder];
      if (page == null) return;
      if (page.offset > 0) {
        nodes.add((
          parent: folder,
          depth: depth,
          row: null,
          offset: (page.offset - 128).clamp(0, page.total),
        ));
      }
      for (final row in page.rows) {
        nodes.add((parent: folder, depth: depth, row: row, offset: -1));
        if (c.expanded.contains(row.path) && depth < 256) {
          append(row.path, depth + 1);
        }
      }
      if (page.offset + page.rows.length < page.total) {
        nodes.add((
          parent: folder,
          depth: depth,
          row: null,
          offset: page.offset + page.rows.length,
        ));
      }
    }

    append(c.treeRoot, 0);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.all(12),
          child: Text(l.folders, style: Theme.of(context).textTheme.labelLarge),
        ),
        TextButton.icon(
          onPressed: () {
            c.focusTree('');
            c.navigate('');
          },
          icon: const AppIcon('archive'),
          label: Text(l.archiveRoot),
        ),
        if (c.treeRoot.isNotEmpty)
          TextButton.icon(
            onPressed: () => c.focusTree(BrowserController.parent(c.treeRoot)),
            icon: const Icon(Icons.arrow_upward, size: 16),
            label: Text(
              c.treeRoot,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
          ),
        Expanded(
          child: ListView.builder(
            itemCount: nodes.length,
            itemBuilder: (context, i) {
              final node = nodes[i];
              final row = node.row;
              if (row == null) {
                return TextButton(
                  onPressed: () =>
                      c.loadFolders(node.parent, offset: node.offset),
                  child: Text(
                    node.offset < (c.folderWindows[node.parent]?.offset ?? 0)
                        ? l.previous
                        : l.more,
                  ),
                );
              }
              return Padding(
                padding: EdgeInsets.only(
                  left: (node.depth * 12.0).clamp(0, 96),
                ),
                child: Row(
                  children: [
                    IconButton(
                      tooltip: l.folders,
                      visualDensity: VisualDensity.compact,
                      onPressed: () => c.expand(row.path),
                      icon: Transform.rotate(
                        angle: c.expanded.contains(row.path) ? 1.5708 : 0,
                        child: const AppIcon('chevron', size: 14),
                      ),
                    ),
                    Expanded(
                      child: TextButton(
                        style: TextButton.styleFrom(
                          alignment: Alignment.centerLeft,
                        ),
                        onPressed: () => c.navigate(row.path),
                        child: Row(
                          children: [
                            const AppIcon('folder', size: 17),
                            const SizedBox(width: 6),
                            Expanded(
                              child: Text(
                                row.name,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
              );
            },
          ),
        ),
      ],
    );
  }
}
