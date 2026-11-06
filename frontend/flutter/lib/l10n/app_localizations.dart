import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:intl/intl.dart' as intl;

import 'app_localizations_en.dart';
import 'app_localizations_zh.dart';

// ignore_for_file: type=lint

/// Callers can lookup localized strings with an instance of AppLocalizations
/// returned by `AppLocalizations.of(context)`.
///
/// Applications need to include `AppLocalizations.delegate()` in their app's
/// `localizationDelegates` list, and the locales they support in the app's
/// `supportedLocales` list. For example:
///
/// ```dart
/// import 'l10n/app_localizations.dart';
///
/// return MaterialApp(
///   localizationsDelegates: AppLocalizations.localizationsDelegates,
///   supportedLocales: AppLocalizations.supportedLocales,
///   home: MyApplicationHome(),
/// );
/// ```
///
/// ## Update pubspec.yaml
///
/// Please make sure to update your pubspec.yaml to include the following
/// packages:
///
/// ```yaml
/// dependencies:
///   # Internationalization support.
///   flutter_localizations:
///     sdk: flutter
///   intl: any # Use the pinned version from flutter_localizations
///
///   # Rest of dependencies
/// ```
///
/// ## iOS Applications
///
/// iOS applications define key application metadata, including supported
/// locales, in an Info.plist file that is built into the application bundle.
/// To configure the locales supported by your app, you’ll need to edit this
/// file.
///
/// First, open your project’s ios/Runner.xcworkspace Xcode workspace file.
/// Then, in the Project Navigator, open the Info.plist file under the Runner
/// project’s Runner folder.
///
/// Next, select the Information Property List item, select Add Item from the
/// Editor menu, then select Localizations from the pop-up menu.
///
/// Select and expand the newly-created Localizations item then, for each
/// locale your application supports, add a new item and select the locale
/// you wish to add from the pop-up menu in the Value field. This list should
/// be consistent with the languages listed in the AppLocalizations.supportedLocales
/// property.
abstract class AppLocalizations {
  AppLocalizations(String locale)
    : localeName = intl.Intl.canonicalizedLocale(locale.toString());

  final String localeName;

  static AppLocalizations? of(BuildContext context) {
    return Localizations.of<AppLocalizations>(context, AppLocalizations);
  }

  static const LocalizationsDelegate<AppLocalizations> delegate =
      _AppLocalizationsDelegate();

  /// A list of this localizations delegate along with the default localizations
  /// delegates.
  ///
  /// Returns a list of localizations delegates containing this delegate along with
  /// GlobalMaterialLocalizations.delegate, GlobalCupertinoLocalizations.delegate,
  /// and GlobalWidgetsLocalizations.delegate.
  ///
  /// Additional delegates can be added by appending to this list in
  /// MaterialApp. This list does not have to be used at all if a custom list
  /// of delegates is preferred or required.
  static const List<LocalizationsDelegate<dynamic>> localizationsDelegates =
      <LocalizationsDelegate<dynamic>>[
        delegate,
        GlobalMaterialLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
      ];

  /// A list of this localizations delegate's supported locales.
  static const List<Locale> supportedLocales = <Locale>[
    Locale('en'),
    Locale('zh'),
    Locale('zh', 'CN'),
  ];

  /// No description provided for @appTitle.
  ///
  /// In en, this message translates to:
  /// **'LiteArc'**
  String get appTitle;

  /// No description provided for @home.
  ///
  /// In en, this message translates to:
  /// **'Home'**
  String get home;

  /// No description provided for @openArchive.
  ///
  /// In en, this message translates to:
  /// **'Open archive'**
  String get openArchive;

  /// No description provided for @closeArchive.
  ///
  /// In en, this message translates to:
  /// **'Close archive'**
  String get closeArchive;

  /// No description provided for @homeTitle.
  ///
  /// In en, this message translates to:
  /// **'Your archives, clearly arranged.'**
  String get homeTitle;

  /// No description provided for @homeBody.
  ///
  /// In en, this message translates to:
  /// **'Open an archive to browse its folders and files.'**
  String get homeBody;

  /// No description provided for @archiveRoot.
  ///
  /// In en, this message translates to:
  /// **'Archive root'**
  String get archiveRoot;

  /// No description provided for @up.
  ///
  /// In en, this message translates to:
  /// **'Up one level'**
  String get up;

  /// No description provided for @name.
  ///
  /// In en, this message translates to:
  /// **'Name'**
  String get name;

  /// No description provided for @size.
  ///
  /// In en, this message translates to:
  /// **'Size'**
  String get size;

  /// No description provided for @type.
  ///
  /// In en, this message translates to:
  /// **'Type'**
  String get type;

  /// No description provided for @modified.
  ///
  /// In en, this message translates to:
  /// **'Modified'**
  String get modified;

  /// No description provided for @search.
  ///
  /// In en, this message translates to:
  /// **'Search this folder'**
  String get search;

  /// No description provided for @empty.
  ///
  /// In en, this message translates to:
  /// **'No items in this folder'**
  String get empty;

  /// No description provided for @selectAll.
  ///
  /// In en, this message translates to:
  /// **'Select all'**
  String get selectAll;

  /// No description provided for @cancel.
  ///
  /// In en, this message translates to:
  /// **'Cancel'**
  String get cancel;

  /// No description provided for @folders.
  ///
  /// In en, this message translates to:
  /// **'Folders'**
  String get folders;

  /// No description provided for @more.
  ///
  /// In en, this message translates to:
  /// **'Next folders'**
  String get more;

  /// No description provided for @previous.
  ///
  /// In en, this message translates to:
  /// **'Previous folders'**
  String get previous;

  /// No description provided for @language.
  ///
  /// In en, this message translates to:
  /// **'Language'**
  String get language;

  /// No description provided for @theme.
  ///
  /// In en, this message translates to:
  /// **'Theme'**
  String get theme;

  /// No description provided for @system.
  ///
  /// In en, this message translates to:
  /// **'Follow system'**
  String get system;

  /// No description provided for @light.
  ///
  /// In en, this message translates to:
  /// **'Light'**
  String get light;

  /// No description provided for @dark.
  ///
  /// In en, this message translates to:
  /// **'Dark'**
  String get dark;

  /// No description provided for @errorTitle.
  ///
  /// In en, this message translates to:
  /// **'Unable to complete the request'**
  String get errorTitle;

  /// No description provided for @details.
  ///
  /// In en, this message translates to:
  /// **'Details'**
  String get details;

  /// No description provided for @retry.
  ///
  /// In en, this message translates to:
  /// **'Retry'**
  String get retry;

  /// No description provided for @loading.
  ///
  /// In en, this message translates to:
  /// **'Loading'**
  String get loading;

  /// No description provided for @file.
  ///
  /// In en, this message translates to:
  /// **'File'**
  String get file;

  /// No description provided for @folder.
  ///
  /// In en, this message translates to:
  /// **'Folder'**
  String get folder;

  /// No description provided for @link.
  ///
  /// In en, this message translates to:
  /// **'Link'**
  String get link;

  /// No description provided for @other.
  ///
  /// In en, this message translates to:
  /// **'Other'**
  String get other;

  /// No description provided for @close.
  ///
  /// In en, this message translates to:
  /// **'Close'**
  String get close;

  /// No description provided for @selectionLimit.
  ///
  /// In en, this message translates to:
  /// **'Too many selection ranges. Use a contiguous range or Select all.'**
  String get selectionLimit;

  /// No description provided for @status.
  ///
  /// In en, this message translates to:
  /// **'{count} items · {selected} selected'**
  String status(int count, int selected);

  /// No description provided for @task.
  ///
  /// In en, this message translates to:
  /// **'{operation} · {state} · {entries} entries · {ms} ms'**
  String task(String operation, String state, int entries, int ms);

  /// No description provided for @operation.
  ///
  /// In en, this message translates to:
  /// **'{value, select, open{Opening archive} query{Querying folder} page{Loading files} folders{Loading folders} selection{Resolving selection} extractAll{Extracting all} extractSelected{Extracting selection} compress{Creating archive} other{Archive task}}'**
  String operation(String value);

  /// No description provided for @taskState.
  ///
  /// In en, this message translates to:
  /// **'{value, select, s1{Pending} s2{Running} s3{Cancelling} s4{Cancelled} s5{Succeeded} s6{Failed} other{Unknown}}'**
  String taskState(String value);

  /// No description provided for @errorMessage.
  ///
  /// In en, this message translates to:
  /// **'{value, select, e1{File not found.} e2{Permission denied.} e3{Unsupported archive format.} e4{Unsupported compression method.} e5{Operation is not supported.} e6{Invalid archive.} e7{The archive is corrupted.} e8{Incorrect password.} e9{A password is required.} e10{The disk is full.} e11{The output already exists.} e12{The operation was cancelled.} e13{Cleanup failed.} e14{A resource limit was reached.} e15{Invalid path encoding.} e16{Unsafe archive path.} e17{An input/output error occurred.} e18{The archive backend reported a warning.} e19{The archive backend reported an error.} other{The request failed. See details.}}'**
  String errorMessage(String value);

  /// No description provided for @components.
  ///
  /// In en, this message translates to:
  /// **'Components and build'**
  String get components;

  /// No description provided for @licenses.
  ///
  /// In en, this message translates to:
  /// **'Licenses'**
  String get licenses;

  /// No description provided for @unavailable.
  ///
  /// In en, this message translates to:
  /// **'Unavailable'**
  String get unavailable;

  /// No description provided for @modifiedBuild.
  ///
  /// In en, this message translates to:
  /// **'modified build'**
  String get modifiedBuild;

  /// No description provided for @createArchive.
  ///
  /// In en, this message translates to:
  /// **'Create archive'**
  String get createArchive;

  /// No description provided for @extractAll.
  ///
  /// In en, this message translates to:
  /// **'Extract all'**
  String get extractAll;

  /// No description provided for @extractSelected.
  ///
  /// In en, this message translates to:
  /// **'Extract selected'**
  String get extractSelected;

  /// No description provided for @extract.
  ///
  /// In en, this message translates to:
  /// **'Extract'**
  String get extract;

  /// No description provided for @password.
  ///
  /// In en, this message translates to:
  /// **'Password'**
  String get password;

  /// No description provided for @overwrite.
  ///
  /// In en, this message translates to:
  /// **'Existing files'**
  String get overwrite;

  /// No description provided for @applyToAll.
  ///
  /// In en, this message translates to:
  /// **'Apply to all in this task'**
  String get applyToAll;

  /// No description provided for @conflictTitle.
  ///
  /// In en, this message translates to:
  /// **'The destination already exists'**
  String get conflictTitle;

  /// No description provided for @staleAnswer.
  ///
  /// In en, this message translates to:
  /// **'This conflict is no longer waiting for an answer.'**
  String get staleAnswer;

  /// No description provided for @cleanupNotice.
  ///
  /// In en, this message translates to:
  /// **'The operation did not complete. Extraction rolls back new output; inspect the destination and cleanup error details. Existing files may already have been replaced.'**
  String get cleanupNotice;

  /// No description provided for @addFiles.
  ///
  /// In en, this message translates to:
  /// **'Add files'**
  String get addFiles;

  /// No description provided for @addFolder.
  ///
  /// In en, this message translates to:
  /// **'Add folder'**
  String get addFolder;

  /// No description provided for @clearInputs.
  ///
  /// In en, this message translates to:
  /// **'Clear inputs'**
  String get clearInputs;

  /// No description provided for @inputLimit.
  ///
  /// In en, this message translates to:
  /// **'At most 4,096 input paths are allowed.'**
  String get inputLimit;

  /// No description provided for @inputCount.
  ///
  /// In en, this message translates to:
  /// **'{count} input paths'**
  String inputCount(int count);

  /// No description provided for @formatMethod.
  ///
  /// In en, this message translates to:
  /// **'Format / method'**
  String get formatMethod;

  /// No description provided for @level.
  ///
  /// In en, this message translates to:
  /// **'Compression level'**
  String get level;

  /// No description provided for @preservePermissions.
  ///
  /// In en, this message translates to:
  /// **'Preserve permissions'**
  String get preservePermissions;

  /// No description provided for @preserveTimestamps.
  ///
  /// In en, this message translates to:
  /// **'Preserve timestamps'**
  String get preserveTimestamps;

  /// No description provided for @chooseOutput.
  ///
  /// In en, this message translates to:
  /// **'Choose output'**
  String get chooseOutput;

  /// No description provided for @replaceArchive.
  ///
  /// In en, this message translates to:
  /// **'Replace existing archive after successful creation'**
  String get replaceArchive;

  /// No description provided for @overwritePolicy.
  ///
  /// In en, this message translates to:
  /// **'{value, select, p0{Ask} p1{Overwrite} p2{Skip} p3{Rename} p4{Overwrite if newer} other{Unknown}}'**
  String overwritePolicy(String value);

  /// No description provided for @operationStats.
  ///
  /// In en, this message translates to:
  /// **'{entries} entries · {bytes} B · {speed} B/s · {ms} ms'**
  String operationStats(String entries, String bytes, String speed, int ms);

  /// No description provided for @compressionPhase.
  ///
  /// In en, this message translates to:
  /// **'{value, select, p1{Scanning inputs} p2{Writing archive} p3{Finalizing output} other{Working}}'**
  String compressionPhase(String value);
}

class _AppLocalizationsDelegate
    extends LocalizationsDelegate<AppLocalizations> {
  const _AppLocalizationsDelegate();

  @override
  Future<AppLocalizations> load(Locale locale) {
    return SynchronousFuture<AppLocalizations>(lookupAppLocalizations(locale));
  }

  @override
  bool isSupported(Locale locale) =>
      <String>['en', 'zh'].contains(locale.languageCode);

  @override
  bool shouldReload(_AppLocalizationsDelegate old) => false;
}

AppLocalizations lookupAppLocalizations(Locale locale) {
  // Lookup logic when language+country codes are specified.
  switch (locale.languageCode) {
    case 'zh':
      {
        switch (locale.countryCode) {
          case 'CN':
            return AppLocalizationsZhCn();
        }
        break;
      }
  }

  // Lookup logic when only language code is specified.
  switch (locale.languageCode) {
    case 'en':
      return AppLocalizationsEn();
    case 'zh':
      return AppLocalizationsZh();
  }

  throw FlutterError(
    'AppLocalizations.delegate failed to load unsupported locale "$locale". This is likely '
    'an issue with the localizations generation tool. Please file an issue '
    'on GitHub with a reproducible sample app and the gen-l10n configuration '
    'that was used.',
  );
}
