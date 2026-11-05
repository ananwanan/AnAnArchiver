import 'package:flutter/material.dart';

import '../l10n/app_localizations.dart';
import '../platform/build_records.dart';

Future<void> showBuildDetails(BuildContext context) {
  final records = loadBuildRecords();
  final l = AppLocalizations.of(context)!;
  return showDialog<void>(
    context: context,
    builder: (context) => AlertDialog(
      title: Text(l.components),
      content: SizedBox(
        width: 700,
        height: 440,
        child: FutureBuilder(
          future: records,
          builder: (context, snapshot) {
            if (snapshot.hasError) {
              return SelectableText('${l.unavailable}\n${snapshot.error}');
            }
            if (!snapshot.hasData) {
              return const Center(child: CircularProgressIndicator());
            }
            final native = snapshot.data!['native'] as Map<String, dynamic>;
            final flutter = snapshot.data!['flutter'] as Map<String, dynamic>;
            final application = native['application'] as Map<String, dynamic>;
            final deps = native['dependencies'] as List<dynamic>;
            return ListView(
              children: [
                SelectableText(
                  '${l.appTitle} ${application['version']}\n${application['revision'] ?? l.unavailable}'
                  '${application['dirty'] == true ? ' (${l.modifiedBuild})' : ''}\nhttps://github.com/ananwanan/AnAnArchiver\n',
                ),
                SelectableText(
                  'Flutter ${flutter['frameworkVersion']} · Dart ${flutter['dartSdkVersion']}\n'
                  '${flutter['frameworkRevision']}\nEngine: ${flutter['engineRevision']}\n${flutter['repositoryUrl']}\n',
                ),
                for (final dependency in deps)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 14),
                    child: SelectableText(
                      '${dependency['name']} ${dependency['version'] ?? l.unavailable}\n'
                      '${dependency['revision'] ?? l.unavailable}\n${dependency['url'] ?? l.unavailable}',
                    ),
                  ),
              ],
            );
          },
        ),
      ),
      actions: [
        TextButton(
          onPressed: () =>
              showLicensePage(context: context, applicationName: l.appTitle),
          child: Text(l.licenses),
        ),
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(l.close),
        ),
      ],
    ),
  );
}
