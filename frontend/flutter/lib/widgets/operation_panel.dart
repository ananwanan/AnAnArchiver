import 'package:flutter/material.dart';

import '../controllers/operation_controller.dart';
import '../l10n/app_localizations.dart';

class OperationPanel extends StatefulWidget {
  const OperationPanel(this.controller, {super.key});
  final OperationController controller;
  @override
  State<OperationPanel> createState() => _OperationPanelState();
}

class _OperationPanelState extends State<OperationPanel> {
  final password = TextEditingController();
  bool all = false, answering = false;
  (int, int)? answered;
  int? task;
  String? answerError;
  @override
  void initState() {
    super.initState();
    widget.controller.passwordReset.addListener(password.clear);
  }

  @override
  void dispose() {
    widget.controller.passwordReset.removeListener(password.clear);
    password.clear();
    password.dispose();
    super.dispose();
  }

  void submitPassword() {
    final value = password.text;
    password.clear();
    widget.controller.answerPassword(value);
  }

  @override
  Widget build(BuildContext context) {
    final c = widget.controller;
    final l = AppLocalizations.of(context)!;
    if (!c.waitingForPassword && password.text.isNotEmpty) password.clear();
    if (!c.visible) return const SizedBox.shrink();
    final p = c.progress;
    if (p != null && task != p.taskId) {
      task = p.taskId;
      all = false;
      answered = null;
      answerError = null;
    }
    final conflict = p?.conflict;
    final key = conflict == null ? null : (p!.taskId, conflict.id);
    final error = c.error ?? p?.error;
    return Material(
      color: Theme.of(context).colorScheme.surfaceContainerLow,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(
                  child: Text(
                    c.waitingForPassword
                        ? l.password
                        : '${l.operation(c.operation ?? 'open')} · ${l.taskState('s${p?.state ?? 1}')}',
                  ),
                ),
                TextButton(
                  onPressed: () {
                    password.clear();
                    if (c.busy || c.waitingForPassword) {
                      c.cancel();
                    } else {
                      c.dismiss();
                    }
                  },
                  child: Text(
                    c.busy || c.waitingForPassword ? l.cancel : l.close,
                  ),
                ),
              ],
            ),
            if (c.waitingForPassword) ...[
              Text(l.errorMessage('e${c.passwordError?.code ?? 9}')),
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      key: const Key('operation-password'),
                      controller: password,
                      autofocus: true,
                      obscureText: true,
                      enableSuggestions: false,
                      autocorrect: false,
                      onSubmitted: (_) => submitPassword(),
                      decoration: InputDecoration(labelText: l.password),
                    ),
                  ),
                  const SizedBox(width: 12),
                  FilledButton(onPressed: submitPassword, child: Text(l.retry)),
                ],
              ),
            ] else if (p != null) ...[
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
              if (p.phase != 0) Text(l.compressionPhase('p${p.phase}')),
              if (p.currentFile.isNotEmpty)
                Text(
                  p.currentFile,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
            ],
            if (c.destination != null && !c.waitingForPassword)
              Text(
                c.destination!,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            if (conflict != null && c.busy && key != answered) ...[
              Text(l.conflictTitle),
              Text(
                conflict.destination,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
              Wrap(
                spacing: 8,
                crossAxisAlignment: WrapCrossAlignment.center,
                children: [
                  for (final policy in [1, 2, 3, 4])
                    OutlinedButton(
                      onPressed: answering
                          ? null
                          : () async {
                              setState(() {
                                answering = true;
                                answerError = null;
                              });
                              try {
                                await c.resolve(p!, policy, all);
                                if (mounted) setState(() => answered = key);
                              } catch (_) {
                                if (mounted) {
                                  setState(() => answerError = l.staleAnswer);
                                }
                              } finally {
                                if (mounted) setState(() => answering = false);
                              }
                            },
                      child: Text(l.overwritePolicy('p$policy')),
                    ),
                  Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Checkbox(
                        value: all,
                        onChanged: answering
                            ? null
                            : (v) => setState(() => all = v!),
                      ),
                      Text(l.applyToAll),
                    ],
                  ),
                ],
              ),
              if (answerError != null) Text(answerError!),
            ],
            if (error != null && !c.waitingForPassword) ...[
              Text(l.errorMessage('e${error.code}')),
              if (error.code == 12 || error.code == 13) Text(l.cleanupNotice),
              TextButton(
                onPressed: () => showDialog<void>(
                  context: context,
                  builder: (context) => AlertDialog(
                    title: Text(l.details),
                    content: SizedBox(
                      width: 600,
                      child: SingleChildScrollView(
                        child: SelectableText(
                          '${l.errorMessage('e${error.code}')}\n${error.message}\n${error.backend}\n${error.path}\nAA status: ${error.status}; code: ${error.code}',
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
          ],
        ),
      ),
    );
  }
}
