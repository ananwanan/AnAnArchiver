import 'package:flutter/material.dart';
import 'package:litearc_binding/litearc_binding.dart';

import '../l10n/app_localizations.dart';
import '../platform/desktop_adapter.dart';

class CreateArchiveDialog extends StatefulWidget {
  const CreateArchiveDialog({
    super.key,
    required this.capabilities,
    required this.platform,
  });
  final List<CompressionCapability> capabilities;
  final DesktopAdapter platform;
  @override
  State<CreateArchiveDialog> createState() => _CreateArchiveDialogState();
}

class _CreateArchiveDialogState extends State<CreateArchiveDialog> {
  final paths = <String>[];
  late CompressionCapability capability = widget.capabilities.first;
  late int level = capability.defaultLevel;
  bool permissions = true, timestamps = true, replace = false, picking = false;
  String? destination, error;
  Future<void> pick(Future<void> Function() action) async {
    setState(() {
      picking = true;
      error = null;
    });
    try {
      await action();
    } catch (e) {
      if (mounted) error = '$e';
    } finally {
      if (mounted) setState(() => picking = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final l = AppLocalizations.of(context)!;
    return AlertDialog(
      title: Text(l.createArchive),
      content: SizedBox(
        width: 600,
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Wrap(
                spacing: 8,
                children: [
                  OutlinedButton(
                    onPressed: picking
                        ? null
                        : () => pick(() async {
                            final inputs = await widget.platform.chooseInputs();
                            if (!mounted) return;
                            if (paths.length + inputs.length > 4096) {
                              error = l.inputLimit;
                              return;
                            }
                            paths.addAll(inputs);
                          }),
                    child: Text(l.addFiles),
                  ),
                  OutlinedButton(
                    onPressed: picking
                        ? null
                        : () => pick(() async {
                            final folder = await widget.platform
                                .chooseDirectory();
                            if (!mounted || folder == null) return;
                            if (paths.length >= 4096) {
                              error = l.inputLimit;
                              return;
                            }
                            paths.add(folder);
                          }),
                    child: Text(l.addFolder),
                  ),
                  TextButton(
                    onPressed: picking ? null : () => setState(paths.clear),
                    child: Text(l.clearInputs),
                  ),
                ],
              ),
              Text(l.inputCount(paths.length)),
              for (final path in paths.take(8))
                Text(path, maxLines: 1, overflow: TextOverflow.ellipsis),
              const SizedBox(height: 12),
              DropdownButtonFormField<CompressionCapability>(
                initialValue: capability,
                isExpanded: true,
                decoration: InputDecoration(labelText: l.formatMethod),
                items: widget.capabilities
                    .map(
                      (c) => DropdownMenuItem(
                        value: c,
                        child: Text('${c.formatName} · ${c.methodName}'),
                      ),
                    )
                    .toList(),
                onChanged: picking
                    ? null
                    : (c) => setState(() {
                        capability = c!;
                        level = c.defaultLevel;
                        destination = null;
                      }),
              ),
              if (capability.hasLevels)
                Padding(
                  padding: const EdgeInsets.only(top: 12),
                  child: TextFormField(
                    key: ValueKey(capability),
                    initialValue: '$level',
                    keyboardType: TextInputType.number,
                    decoration: InputDecoration(
                      labelText:
                          '${l.level} (${capability.minLevel}–${capability.maxLevel})',
                    ),
                    onChanged: (value) => setState(
                      () => level =
                          int.tryParse(value) ?? capability.minLevel - 1,
                    ),
                  ),
                ),
              if (capability.permissions)
                CheckboxListTile(
                  contentPadding: EdgeInsets.zero,
                  title: Text(l.preservePermissions),
                  value: permissions,
                  onChanged: (v) => setState(() => permissions = v!),
                ),
              if (capability.timestamps)
                CheckboxListTile(
                  contentPadding: EdgeInsets.zero,
                  title: Text(l.preserveTimestamps),
                  value: timestamps,
                  onChanged: (v) => setState(() => timestamps = v!),
                ),
              OutlinedButton(
                onPressed: picking
                    ? null
                    : () => pick(() async {
                        final path = await widget.platform.chooseOutput(
                          'archive.${capability.extension.replaceFirst(RegExp(r'^\.'), '')}',
                        );
                        if (mounted && path != null) destination = path;
                      }),
                child: Text(l.chooseOutput),
              ),
              if (destination != null)
                Text(
                  destination!,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                ),
              CheckboxListTile(
                contentPadding: EdgeInsets.zero,
                title: Text(l.replaceArchive),
                value: replace,
                onChanged: (v) => setState(() => replace = v!),
              ),
              if (error != null) Text('${l.errorTitle}: $error'),
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(l.cancel),
        ),
        FilledButton(
          onPressed:
              picking ||
                  paths.isEmpty ||
                  destination == null ||
                  (capability.hasLevels &&
                      (level < capability.minLevel ||
                          level > capability.maxLevel))
              ? null
              : () => Navigator.pop(context, <String, Object?>{
                  'paths': List<String>.of(paths),
                  'destination': destination,
                  'format': capability.format,
                  'method': capability.method,
                  'level': level,
                  'permissions': capability.permissions && permissions,
                  'timestamps': capability.timestamps && timestamps,
                  'replace': replace,
                }),
          child: Text(l.createArchive),
        ),
      ],
    );
  }
}
