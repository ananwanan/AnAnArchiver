import 'package:flutter/material.dart';

import '../controllers/browser_controller.dart';
import '../l10n/app_localizations.dart';

Future<void> showTaskCenter(
  BuildContext context,
  BrowserController c,
) => showDialog<void>(
  context: context,
  builder: (context) {
    final l = AppLocalizations.of(context)!;
    return AnimatedBuilder(
      animation: c.history,
      builder: (context, _) => AlertDialog(
        title: Text(l.taskCenter),
        content: SizedBox(
          width: 720,
          height: 450,
          child: c.history.tasks.isEmpty
              ? Center(child: Text(l.noTasks))
              : ListView(
                  children: [
                    for (final p in c.history.tasks.values.toList().reversed)
                      ListTile(
                        isThreeLine: true,
                        title: Text(
                          '${l.operation(p.operation == 'select' ? 'selection' : p.operation)} · ${l.taskState('s${p.state}')}',
                        ),
                        subtitle: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            if (!p.terminal)
                              LinearProgressIndicator(
                                value: p.totalBytes != null && p.totalBytes! > 0
                                    ? (p.bytes / p.totalBytes!).clamp(0, 1)
                                    : null,
                              ),
                            Text(
                              l.operationStats(
                                '${p.entries}${p.totalEntries == null ? '' : ' / ${p.totalEntries}'}',
                                '${p.bytes}${p.totalBytes == null ? '' : ' / ${p.totalBytes}'}',
                                p.bytesPerSecond.toStringAsFixed(0),
                                p.elapsedMs,
                              ),
                            ),
                            if (p.currentFile.isNotEmpty)
                              Text(
                                p.currentFile,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                              ),
                            if (c.history.destinations[p.taskId]
                                case final destination?)
                              Text(
                                destination,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                              ),
                            if (p.error != null)
                              Text(l.errorMessage('e${p.error!.code}')),
                          ],
                        ),
                        trailing: p.terminal
                            ? (p.error == null
                                  ? null
                                  : IconButton(
                                      tooltip: l.details,
                                      icon: const Icon(Icons.info_outline),
                                      onPressed: () => showDialog<void>(
                                        context: context,
                                        builder: (context) => AlertDialog(
                                          title: Text(l.details),
                                          content: SizedBox(
                                            width: 600,
                                            child: SingleChildScrollView(
                                              child: SelectableText(
                                                '${p.error!.message}\n${p.error!.backend}\n${p.error!.path}',
                                              ),
                                            ),
                                          ),
                                          actions: [
                                            TextButton(
                                              onPressed: () =>
                                                  Navigator.pop(context),
                                              child: Text(l.close),
                                            ),
                                          ],
                                        ),
                                      ),
                                    ))
                            : TextButton(
                                onPressed: () => p.isArchiveOperation
                                    ? c.operations.cancel()
                                    : c.cancelBrowsing(),
                                child: Text(l.cancel),
                              ),
                      ),
                  ],
                ),
        ),
        actions: [
          TextButton(onPressed: c.history.clear, child: Text(l.clearHistory)),
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: Text(l.close),
          ),
        ],
      ),
    );
  },
);
