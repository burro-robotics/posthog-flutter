enum PostHogPersonProfiles { never, always, identifiedOnly }

enum PostHogDataMode { wifi, cellular, any }

class PostHogConfig {
  final String apiKey;
  var host = 'https://us.i.posthog.com';
  var flushAt = 20;
  var maxQueueSize = 1000;
  var maxBatchSize = 50;
  var flushInterval = const Duration(seconds: 30);
  var sendFeatureFlagEvents = true;
  var preloadFeatureFlags = true;
  var captureApplicationLifecycleEvents = false;

  var debug = false;
  var optOut = false;
  var personProfiles = PostHogPersonProfiles.identifiedOnly;

  /// Enable Recording of Session replay for Android and iOS.
  /// Requires Record user sessions to be enabled in the PostHog Project Settings.
  /// Defaults to false.
  var sessionReplay = false;

  /// Configurations for Session replay.
  /// [sessionReplay] has to be enabled for this to take effect.
  var sessionReplayConfig = PostHogSessionReplayConfig();

  /// iOS only
  var dataMode = PostHogDataMode.any;

  /// Enable Surveys
  ///
  /// **Notes:**
  /// - After calling `Posthog().close()`, surveys will not be rendered until the SDK is re-initialized and the next navigation event occurs.
  /// - You must install `PosthogObserver` in your app for surveys to display
  ///   - See: https://posthog.com/docs/surveys/installation?tab=Flutter#step-two-install-posthogobserver
  /// - For Flutter web, this setting will be ignored. Surveys on web use the JavaScript Web SDK instead.
  ///   - See: https://posthog.com/docs/surveys/installation?tab=Web
  ///
  /// Defaults to true.
  var surveys = true;

  /// Enable Autocapture
  ///
  /// **Notes:**
  /// - Autocapture captures click/tap events with element position and metadata.
  /// - Requires wrapping your app with `PostHogWidget` or `PostHogAutocaptureWidget`.
  /// - Autocapture data is sent as `$autocapture` events with `$elements` array.
  /// - This is separate from PostHog's visual heatmap feature (which requires web/iframe).
  /// - For Flutter web, this setting will be ignored. Autocapture on web uses the JavaScript Web SDK instead.
  ///
  /// Defaults to false.
  var autocapture = false;

  /// Configuration for error tracking and exception capture
  final errorTrackingConfig = PostHogErrorTrackingConfig();

  // TODO: missing getAnonymousId, propertiesSanitizer, captureDeepLinks
  // onFeatureFlags, integrations

  PostHogConfig(this.apiKey);

  Map<String, dynamic> toMap() {
    return {
      'apiKey': apiKey,
      'host': host,
      'flushAt': flushAt,
      'maxQueueSize': maxQueueSize,
      'maxBatchSize': maxBatchSize,
      'flushInterval': flushInterval.inSeconds,
      'sendFeatureFlagEvents': sendFeatureFlagEvents,
      'preloadFeatureFlags': preloadFeatureFlags,
      'captureApplicationLifecycleEvents': captureApplicationLifecycleEvents,
      'debug': debug,
      'optOut': optOut,
      'surveys': surveys,
      'personProfiles': personProfiles.name,
      'sessionReplay': sessionReplay,
      'autocapture': autocapture,
      'dataMode': dataMode.name,
      'sessionReplayConfig': sessionReplayConfig.toMap(),
      'errorTrackingConfig': errorTrackingConfig.toMap(),
    };
  }
}

class PostHogSessionReplayConfig {
  /// Enable masking of all text and text input fields.
  /// Default: true.
  var maskAllTexts = true;

  /// Enable masking of all images.
  /// Default: true.
  var maskAllImages = true;

  /// The value assigned to this var will be forwarded to [throttleDelay]
  ///
  /// Debouncer delay used to reduce the number of snapshots captured and reduce performance impact.
  /// This is used for capturing the view as a screenshot.
  /// The lower the number, the more snapshots will be captured but higher the performance impact.
  /// Defaults to 1s.
  @Deprecated('Deprecated in favor of [throttleDelay] from v4.8.0.')
  set debouncerDelay(Duration debouncerDelay) {
    throttleDelay = debouncerDelay;
  }

  /// Throttling delay used to reduce the number of snapshots captured and reduce performance impact.
  /// This is used for capturing the view as a screenshot.
  /// The lower the number, the more snapshots will be captured but higher the performance impact.
  /// Defaults to 1s.
  var throttleDelay = const Duration(seconds: 1);

  /// JPEG compression quality for session replay screenshots (0-100).
  /// Lower values = smaller file size but lower image quality.
  /// For low bandwidth IoT devices, consider values between 40-70.
  /// Defaults to 75.
  var compressionQuality = 40;

  /// Maximum number of snapshots to buffer before sending a batch.
  /// Larger batches = fewer network requests but more memory usage.
  /// Defaults to 10.
  var batchSize = 10;

  /// Maximum time to wait before sending a batch, even if batchSize is not reached.
  /// This ensures snapshots are sent even during low activity periods.
  /// Defaults to 5 seconds.
  var batchInterval = const Duration(seconds: 5);

  /// Maximum width/height to resize screenshots to before compression.
  /// Set to 0 to disable resizing. Useful for very low bandwidth scenarios.
  /// Defaults to 0 (no resizing).
  var maxImageDimension = 0;

  /// Enable pausing session replay recording after a period of user inactivity.
  /// When enabled, recording will pause after [idleTimeout] and resume on any user interaction.
  /// This is useful for apps with idle animations or background updates that shouldn't be recorded.
  /// Defaults to false (recording continues regardless of user interaction).
  var pauseOnIdle = false;

  /// Duration of inactivity before pausing session replay recording.
  /// Only used when [pauseOnIdle] is true.
  /// Defaults to 5 seconds.
  var idleTimeout = const Duration(seconds: 5);

  /// Duration of inactivity before creating a new session on next interaction.
  /// If idle period exceeds this duration, a new session will be created when user interacts again.
  /// This is useful for kiosk mode applications where users interact, robot moves, then interact again later.
  /// Must be longer than [idleTimeout] to take effect.
  /// Only used when [pauseOnIdle] is true.
  /// Defaults to 5 minutes (300 seconds).
  var sessionTimeout = const Duration(minutes: 5);

  Map<String, dynamic> toMap() {
    return {
      'maskAllImages': maskAllImages,
      'maskAllTexts': maskAllTexts,
      'throttleDelayMs': throttleDelay.inMilliseconds,
      'compressionQuality': compressionQuality,
      'batchSize': batchSize,
      'batchIntervalMs': batchInterval.inMilliseconds,
      'maxImageDimension': maxImageDimension,
      'pauseOnIdle': pauseOnIdle,
      'idleTimeoutMs': idleTimeout.inMilliseconds,
      'sessionTimeoutMs': sessionTimeout.inMilliseconds,
    };
  }
}

class PostHogErrorTrackingConfig {
  /// List of package names to be considered inApp frames for exception tracking
  ///
  /// inApp Example:
  /// inAppIncludes = ["package:your_app", "package:your_company_utils"]
  /// All exception stacktrace frames from these packages will be considered inApp
  ///
  /// This option takes precedence over inAppExcludes.
  /// For Flutter/Dart, this typically includes:
  /// - Your app's main package (e.g., "package:your_app")
  /// - Any internal packages you own (e.g., "package:your_company_utils")
  ///
  /// **Note:**
  /// - Flutter web: Not supported
  ///
  final inAppIncludes = <String>[];

  /// List of package names to be excluded from inApp frames for exception tracking
  ///
  /// inAppExcludes Example:
  /// inAppExcludes = ["package:third_party_lib", "package:analytics_package"]
  /// All exception stacktrace frames from these packages will be considered external
  ///
  /// Note: inAppIncludes takes precedence over this setting.
  /// Common packages to exclude:
  /// - Third-party analytics packages
  /// - External utility libraries
  /// - Packages you don't control
  ///
  /// **Note:**
  /// - Flutter web: Not supported
  ///
  final inAppExcludes = <String>[];

  /// Configures whether stack trace frames are considered inApp by default
  /// when the origin cannot be determined or no explicit includes/excludes match.
  ///
  /// - If true: Frames are inApp unless explicitly excluded (allowlist approach)
  /// - If false: Frames are external unless explicitly included (denylist approach)
  ///
  /// Default behavior when true:
  /// - Local files (no package prefix) are inApp
  /// - dart and flutter packages are excluded
  /// - All other packages are inApp unless in inAppExcludes
  ///
  /// **Note:**
  /// - Flutter web: Not supported
  ///
  var inAppByDefault = true;

  /// Enable automatic capture of Flutter framework errors
  ///
  /// Controls whether `FlutterError.onError` errors are captured.
  ///
  /// **Note:**
  /// - Flutter web: Not supported
  ///
  /// Default: true (when autocapture is enabled)
  var captureFlutterErrors = false;

  /// Enable capturing of silent Flutter errors
  ///
  /// Controls whether Flutter errors marked as silent (FlutterErrorDetails.silent = true) are captured.
  ///
  /// **Note:**
  /// - Flutter web: Not supported
  ///
  /// Default: false
  var captureSilentFlutterErrors = false;

  /// Enable automatic capture of Dart runtime errors
  ///
  /// Controls whether `PlatformDispatcher.onError errors` are captured.
  ///
  /// **Note:**
  /// - Flutter web: Not supported
  ///
  /// Default: true
  var capturePlatformDispatcherErrors = false;

  /// Enable automatic capture of exceptions in the native SDKs (Android only for now)
  ///
  /// Controls whether native exceptions are captured.
  ///
  /// **Note:**
  /// - iOS: Not supported
  /// - Android: Java/Kotlin exceptions only (no native C/C++ crashes)
  /// - Android: No stacktrace demangling for minified builds
  ///
  /// Default: true
  var captureNativeExceptions = false;

  /// Enable automatic capture of isolate errors
  ///
  /// Controls whether errors from the current isolate are captured.
  /// This includes errors from the main isolate and any isolates spawned
  /// without explicit error handling.
  ///
  /// **Note:**
  /// - Flutter web: Not supported
  ///
  /// Default: true
  var captureIsolateErrors = false;

  Map<String, dynamic> toMap() {
    return {
      'inAppIncludes': inAppIncludes,
      'inAppExcludes': inAppExcludes,
      'inAppByDefault': inAppByDefault,
      'captureFlutterErrors': captureFlutterErrors,
      'captureSilentFlutterErrors': captureSilentFlutterErrors,
      'capturePlatformDispatcherErrors': capturePlatformDispatcherErrors,
      'captureNativeExceptions': captureNativeExceptions,
      'captureIsolateErrors': captureIsolateErrors,
    };
  }
}
