// ignore: unused_import
import 'package:intl/intl.dart' as intl;

import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for English (`en`).
class AppLocalizationsEn extends AppLocalizations {
  AppLocalizationsEn([String locale = 'en']) : super(locale);

  @override
  String get appTitle => 'LiteArc';

  @override
  String get home => 'Home';

  @override
  String get openArchive => 'Open archive';

  @override
  String get closeArchive => 'Close archive';

  @override
  String get homeTitle => 'Your archives, clearly arranged.';

  @override
  String get homeBody => 'Open an archive to browse its folders and files.';

  @override
  String get archiveRoot => 'Archive root';

  @override
  String get up => 'Up one level';

  @override
  String get name => 'Name';

  @override
  String get size => 'Size';

  @override
  String get type => 'Type';

  @override
  String get modified => 'Modified';

  @override
  String get search => 'Search this folder';

  @override
  String get empty => 'No items in this folder';

  @override
  String get selectAll => 'Select all';

  @override
  String get cancel => 'Cancel';

  @override
  String get folders => 'Folders';

  @override
  String get more => 'Next folders';

  @override
  String get previous => 'Previous folders';

  @override
  String get language => 'Language';

  @override
  String get theme => 'Theme';

  @override
  String get system => 'Follow system';

  @override
  String get light => 'Light';

  @override
  String get dark => 'Dark';

  @override
  String get errorTitle => 'Unable to complete the request';

  @override
  String get details => 'Details';

  @override
  String get retry => 'Retry';

  @override
  String get loading => 'Loading';

  @override
  String get file => 'File';

  @override
  String get folder => 'Folder';

  @override
  String get link => 'Link';

  @override
  String get other => 'Other';

  @override
  String get close => 'Close';

  @override
  String get selectionLimit =>
      'Too many selection ranges. Use a contiguous range or Select all.';

  @override
  String status(int count, int selected) {
    return '$count items · $selected selected';
  }

  @override
  String task(String operation, String state, int entries, int ms) {
    return '$operation · $state · $entries entries · $ms ms';
  }

  @override
  String operation(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'open': 'Opening archive',
      'query': 'Querying folder',
      'page': 'Loading files',
      'folders': 'Loading folders',
      'selection': 'Resolving selection',
      'extractAll': 'Extracting all',
      'extractSelected': 'Extracting selection',
      'compress': 'Creating archive',
      'export': 'Preparing desktop files',
      'probe': 'Detecting dropped file',
      'other': 'Archive task',
    });
    return '$_temp0';
  }

  @override
  String taskState(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      's1': 'Pending',
      's2': 'Running',
      's3': 'Cancelling',
      's4': 'Cancelled',
      's5': 'Succeeded',
      's6': 'Failed',
      'other': 'Unknown',
    });
    return '$_temp0';
  }

  @override
  String errorMessage(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'e1': 'File not found.',
      'e2': 'Permission denied.',
      'e3': 'Unsupported archive format.',
      'e4': 'Unsupported compression method.',
      'e5': 'Operation is not supported.',
      'e6': 'Invalid archive.',
      'e7': 'The archive is corrupted.',
      'e8': 'Incorrect password.',
      'e9': 'A password is required.',
      'e10': 'The disk is full.',
      'e11': 'The output already exists.',
      'e12': 'The operation was cancelled.',
      'e13': 'Cleanup failed.',
      'e14': 'A resource limit was reached.',
      'e15': 'Invalid path encoding.',
      'e16': 'Unsafe archive path.',
      'e17': 'An input/output error occurred.',
      'e18': 'The archive backend reported a warning.',
      'e19': 'The archive backend reported an error.',
      'other': 'The request failed. See details.',
    });
    return '$_temp0';
  }

  @override
  String get components => 'Components and build';

  @override
  String get licenses => 'Licenses';

  @override
  String get unavailable => 'Unavailable';

  @override
  String get modifiedBuild => 'modified build';

  @override
  String get createArchive => 'Create archive';

  @override
  String get extractAll => 'Extract all';

  @override
  String get extractSelected => 'Extract selected';

  @override
  String get extract => 'Extract';

  @override
  String get password => 'Password';

  @override
  String get overwrite => 'Existing files';

  @override
  String get applyToAll => 'Apply to all in this task';

  @override
  String get conflictTitle => 'The destination already exists';

  @override
  String get staleAnswer => 'This conflict is no longer waiting for an answer.';

  @override
  String get cleanupNotice =>
      'The operation did not complete. Extraction rolls back new output; inspect the destination and cleanup error details. Existing files may already have been replaced.';

  @override
  String get addFiles => 'Add files';

  @override
  String get addFolder => 'Add folder';

  @override
  String get clearInputs => 'Clear inputs';

  @override
  String get inputLimit => 'At most 4,096 input paths are allowed.';

  @override
  String inputCount(int count) {
    return '$count input paths';
  }

  @override
  String get formatMethod => 'Format / method';

  @override
  String get level => 'Compression level';

  @override
  String get preservePermissions => 'Preserve permissions';

  @override
  String get preserveTimestamps => 'Preserve timestamps';

  @override
  String get chooseOutput => 'Choose output';

  @override
  String get replaceArchive =>
      'Replace existing archive after successful creation';

  @override
  String overwritePolicy(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'p0': 'Ask',
      'p1': 'Overwrite',
      'p2': 'Skip',
      'p3': 'Rename',
      'p4': 'Overwrite if newer',
      'other': 'Unknown',
    });
    return '$_temp0';
  }

  @override
  String operationStats(String entries, String bytes, String speed, int ms) {
    return '$entries entries · $bytes B · $speed B/s · $ms ms';
  }

  @override
  String compressionPhase(String value) {
    String _temp0 = intl.Intl.selectLogic(value, {
      'p1': 'Scanning inputs',
      'p2': 'Writing archive',
      'p3': 'Finalizing output',
      'other': 'Working',
    });
    return '$_temp0';
  }

  @override
  String get taskCenter => 'Task center';

  @override
  String get noTasks => 'No tasks in this session.';

  @override
  String get clearHistory => 'Clear history';

  @override
  String get recentArchives => 'Recent archives';

  @override
  String get removeRecent => 'Remove from history';

  @override
  String get settingsProblem =>
      'Preferences could not be loaded or saved. Original data was preserved. See details.';

  @override
  String get prepareDrag => 'Prepare selected files for dragging';

  @override
  String get dragReady => 'Drag prepared files';

  @override
  String get exportFiles => 'Temporary exports';

  @override
  String get exportRetention =>
      'Desktop targets may read files after a drag or after LiteArc exits. Handed-off output stays in these private folders across restarts. Release only after all target copies have finished. Four exports / 512 MiB each; prepared output expires after ten minutes before handoff.';

  @override
  String get releaseExports => 'All target copies finished: release files';
}
