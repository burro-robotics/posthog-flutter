# PostHog Flutter Integration Guide

This guide explains how to integrate PostHog into a Flutter application, covering all features including event tracking, session replay, autocapture, surveys, feature flags, and error tracking.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Configuration](#configuration)
3. [Event Tracking](#event-tracking)
4. [Session Replay](#session-replay)
5. [Autocapture](#autocapture)
6. [Feature Flags](#feature-flags)
7. [Surveys](#surveys)
8. [Error Tracking](#error-tracking)
9. [User Identification](#user-identification)
10. [Screen Tracking](#screen-tracking)
11. [Best Practices](#best-practices)
12. [Troubleshooting](#troubleshooting)

---

## Quick Start

### Step 1: Add Dependency

Add PostHog Flutter to your `pubspec.yaml`:

```yaml
dependencies:
  flutter:
    sdk: flutter
  posthog_flutter: ^5.9.0
```

Then run:
```bash
flutter pub get
```

### Step 2: Initialize PostHog

Initialize PostHog **before** calling `runApp()`. This is critical for proper setup:

```dart
import 'package:flutter/material.dart';
import 'package:posthog_flutter/posthog_flutter.dart';

Future<void> main() async {
  // CRITICAL: Initialize Flutter bindings first
  WidgetsFlutterBinding.ensureInitialized();

  // Configure PostHog
  final config = PostHogConfig('your-api-key-here');
  config.debug = true; // Set to true for development, false for production
  config.host = 'https://us.i.posthog.com'; // or 'https://eu.i.posthog.com' for EU
  
  // Optional: Enable features you need
  config.sessionReplay = true; // Enable session replay
  config.autocapture = true; // Enable autocapture
  config.surveys = true; // Enable surveys (mobile only)
  
  // CRITICAL: Setup PostHog before runApp()
  await Posthog().setup(config);
  
  runApp(MyApp());
}
```

### Step 3: Wrap Your App (Required for Session Replay and Autocapture)

If you're using session replay or autocapture, wrap your app root with `PostHogWidget`:

```dart
class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return PostHogWidget(
      // PostHogWidget automatically enables session replay and/or autocapture
      // based on your PostHogConfig settings - no additional configuration needed
      child: MaterialApp(
        title: 'My App',
        navigatorObservers: [
          PosthogObserver(), // Required for screen tracking and surveys
        ],
        home: HomeScreen(),
      ),
    );
  }
}
```

**Important Notes**:
- `PostHogWidget` automatically checks your config and enables:
  - Session replay if `config.sessionReplay = true`
  - Autocapture if `config.autocapture = true`
  - Both if both are enabled
  - Neither if both are disabled (just returns the child widget)
- `PosthogObserver` is required for:
  - Automatic screen tracking
  - Surveys (mobile platforms)
  - Screen names in event properties

### Step 4: Verify Setup

Enable debug mode and check logs to verify events are being sent:

```dart
config.debug = true; // In your setup code
```

You should see logs like:
```
[PostHog] Sending 1 events to /capture/
[PostHog] Sent 1 events successfully
```

---

## Configuration

### Basic Configuration

```dart
final config = PostHogConfig('your-api-key-here');

// Basic settings
config.host = 'https://us.i.posthog.com';  // Your PostHog host
config.debug = false;  // Enable debug logging
config.optOut = false;  // Opt out of tracking

// Batching settings
config.flushAt = 20;  // Flush after 20 events
config.maxQueueSize = 1000;  // Max events in queue
config.maxBatchSize = 50;  // Max events per batch
config.flushInterval = const Duration(seconds: 30);  // Auto-flush interval

// Feature flags
config.sendFeatureFlagEvents = true;  // Send events when flags are evaluated
config.preloadFeatureFlags = true;  // Preload flags on startup

// Lifecycle events
config.captureApplicationLifecycleEvents = false;  // Auto-capture app lifecycle
```

### Feature-Specific Configuration

#### Session Replay

```dart
config.sessionReplay = true;
config.sessionReplayConfig.maskAllTexts = true;  // Mask all text
config.sessionReplayConfig.maskAllImages = true;  // Mask all images
config.sessionReplayConfig.throttleDelay = const Duration(seconds: 1);  // Capture frequency
config.sessionReplayConfig.compressionQuality = 75;  // JPEG quality (0-100)
config.sessionReplayConfig.batchSize = 10;  // Snapshots per batch
config.sessionReplayConfig.batchInterval = const Duration(seconds: 5);  // Batch timeout
config.sessionReplayConfig.maxImageDimension = 0;  // Max width/height (0 = no resize)

// Pause recording during idle periods (useful for apps with idle animations)
config.sessionReplayConfig.pauseOnIdle = true;
config.sessionReplayConfig.idleTimeout = const Duration(seconds: 5);

// Create new session after long idle periods (kiosk mode / robotics)
// If idle period exceeds sessionTimeout, a new session is created on next interaction
config.sessionReplayConfig.sessionTimeout = const Duration(minutes: 5);
```

#### Autocapture

```dart
config.autocapture = true;  // Enable click/tap tracking
```

#### Surveys

```dart
config.surveys = true;  // Enable surveys (requires PosthogObserver)
```

#### Error Tracking

```dart
config.errorTrackingConfig.captureFlutterErrors = true;  // Flutter framework errors
config.errorTrackingConfig.capturePlatformDispatcherErrors = true;  // Dart runtime errors
config.errorTrackingConfig.captureNativeExceptions = true;  // Native exceptions (Android)
config.errorTrackingConfig.captureIsolateErrors = true;  // Isolate errors
config.errorTrackingConfig.captureSilentFlutterErrors = false;  // Silent Flutter errors

// Filter stack traces
config.errorTrackingConfig.inAppIncludes = ['package:your_app'];
config.errorTrackingConfig.inAppExcludes = ['package:third_party'];
config.errorTrackingConfig.inAppByDefault = true;
```

---

## Event Tracking

### Basic Event Capture

```dart
// Simple event
await Posthog().capture(
  eventName: 'button_clicked',
);

// Event with properties
await Posthog().capture(
  eventName: 'item_added',
  properties: {
    'item_id': '123',
    'item_name': 'Widget',
    'price': 29.99,
    'category': 'tools',
  },
);
```

### Screen Tracking

```dart
// Track screen views
await Posthog().screen(
  screenName: 'home_screen',
  properties: {
    'user_type': 'premium',
  },
);

// Screen names are automatically added to all events
// when PosthogObserver is installed
```

### Custom Events with Context

```dart
// Register super properties (included in all events)
await Posthog().register('user_role', 'admin');
await Posthog().register('app_version', '1.2.3');

// All subsequent events will include these properties
await Posthog().capture(eventName: 'button_clicked');

// Remove super property
await Posthog().unregister('user_role');
```

---

## Session Replay

### Setup

Session replay records visual snapshots of user sessions for playback in PostHog.

```dart
// 1. Enable in config
config.sessionReplay = true;

// 2. Wrap your app with PostHogWidget
PostHogWidget(
  child: MaterialApp(
    // ... your app
  ),
)
```

### Configuration Options

**Throttling**: Control how often snapshots are captured
```dart
config.sessionReplayConfig.throttleDelay = const Duration(seconds: 1);
```

**Compression**: Balance quality vs. file size
```dart
config.sessionReplayConfig.compressionQuality = 75;  // 0-100
```

**Batching**: Control network usage
```dart
config.sessionReplayConfig.batchSize = 10;  // Snapshots per batch
config.sessionReplayConfig.batchInterval = const Duration(seconds: 5);
```

**Idle Detection**: Pause recording during inactivity
```dart
config.sessionReplayConfig.pauseOnIdle = true;
config.sessionReplayConfig.idleTimeout = const Duration(seconds: 5);
```

**Session Timeout** (Kiosk Mode): Create new session after long idle periods
```dart
// If idle period exceeds sessionTimeout, a new session is created on next interaction
// Useful for kiosk mode or robotics applications where users interact, robot moves, then interact again later
config.sessionReplayConfig.sessionTimeout = const Duration(minutes: 5);
// Must be longer than idleTimeout to take effect
```

**Masking**: Protect sensitive data
```dart
config.sessionReplayConfig.maskAllTexts = true;
config.sessionReplayConfig.maskAllImages = true;
```

### How It Works

- Captures screenshots automatically when UI changes
- Throttled to reduce performance impact
- Deduplicates identical screenshots
- Batched and sent to PostHog
- Can be paused during idle periods

---

## Autocapture

### Setup

Autocapture automatically tracks click/tap events with element metadata.

```dart
// 1. Enable in config
config.autocapture = true;

// 2. Wrap your app with PostHogWidget (it automatically enables autocapture)
PostHogWidget(
  child: MaterialApp(
    // ... your app
  ),
)
```

**Note**: `PostHogWidget` automatically enables autocapture when `config.autocapture = true`. You don't need to use `PostHogAutocaptureWidget` separately.

### What Gets Captured

Each tap/click event includes:
- **X, Y coordinates** (relative to element)
- **Element hierarchy** (parent widgets)
- **Widget type** (e.g., `ElevatedButton`, `Text`)
- **Widget key** (if available)
- **Text content** (if available)
- **Viewport dimensions**
- **Screen name** (if using `PosthogObserver`)

### Widget Keys for Better Tracking

Use consistent keys to identify widgets:

```dart
import 'package:posthog_flutter/posthog_flutter.dart';

// Use PostHogKeys helper for consistency
ElevatedButton(
  key: PostHogKeys.button('add_item'),
  onPressed: () => _addItem(),
  child: Text('Add Item'),
)

TextField(
  key: PostHogKeys.input('search'),
  // ...
)

IconButton(
  key: PostHogKeys.iconButton('settings'),
  icon: Icon(Icons.settings),
)
```

### Key Naming Conventions

| Widget Type | Prefix | Example |
|------------|--------|---------|
| Buttons | `btn_` | `btn_add_item`, `btn_delete` |
| Menus | `menu_` | `menu_file`, `menu_edit` |
| Cards | `card_` | `card_product`, `card_user` |
| Inputs | `input_` | `input_search`, `input_email` |
| Dialogs | `dialog_` | `dialog_confirm`, `dialog_settings` |
| Tabs | `tab_` | `tab_home`, `tab_profile` |
| Lists | `list_` | `list_products`, `list_users` |

### Filtering Noise

In PostHog dashboard, filter autocapture events by:
- **Screen name**: Only show clicks on specific screens
- **Widget type**: Filter by `tag_name` (e.g., only `ElevatedButton`)
- **Text content**: Filter by `text` property
- **Widget key**: Filter by `attr__id` (your keys)

---

## Feature Flags

### Check Feature Flags

```dart
// Check if feature is enabled
final isEnabled = await Posthog().isFeatureEnabled('new_ui');
if (isEnabled) {
  // Show new UI
}

// Get feature flag value
final flagValue = await Posthog().getFeatureFlag('pricing_tier');
if (flagValue == 'premium') {
  // Show premium features
}

// Get feature flag payload (JSON)
final payload = await Posthog().getFeatureFlagPayload('experiment_config');
if (payload != null) {
  // Use payload data
}
```

### Reload Feature Flags

```dart
// Manually reload flags
await Posthog().reloadFeatureFlags();
```

### Configuration

```dart
config.preloadFeatureFlags = true;  // Load flags on startup
config.sendFeatureFlagEvents = true;  // Send events when flags evaluated
```

---

## Surveys

### Setup

```dart
// 1. Enable in config
config.surveys = true;

// 2. Add PosthogObserver to navigator observers
MaterialApp(
  navigatorObservers: [PosthogObserver()],
  // ... your app
)
```

### How It Works

- Surveys are automatically displayed based on PostHog project settings
- Triggered by navigation events
- No additional code needed
- Automatically tracks survey responses

**Note**: Surveys require `PosthogObserver` to be installed in your navigator observers.

---

## Error Tracking

### Setup

```dart
config.errorTrackingConfig.captureFlutterErrors = true;
config.errorTrackingConfig.capturePlatformDispatcherErrors = true;
config.errorTrackingConfig.captureNativeExceptions = true;  // Android only
config.errorTrackingConfig.captureIsolateErrors = true;
```

### Manual Exception Capture

```dart
try {
  // Your code
} catch (e, stackTrace) {
  await Posthog().captureException(
    error: e,
    stackTrace: stackTrace,
    properties: {
      'context': 'checkout_flow',
      'user_id': userId,
    },
  );
  rethrow;
}
```

### Stack Trace Filtering

Configure which stack frames are considered "in-app":

```dart
// Include your packages
config.errorTrackingConfig.inAppIncludes = [
  'package:your_app',
  'package:your_company_utils',
];

// Exclude third-party packages
config.errorTrackingConfig.inAppExcludes = [
  'package:third_party_lib',
  'package:analytics_package',
];

// Default behavior
config.errorTrackingConfig.inAppByDefault = true;  // Allowlist approach
```

---

## User Identification

### Identify Users

```dart
// Identify a user
await Posthog().identify(
  userId: 'user_123',
  userProperties: {
    'name': 'John Doe',
    'email': 'john@example.com',
    'plan': 'premium',
  },
);

// Set properties only once (won't overwrite existing)
await Posthog().identify(
  userId: 'user_123',
  userPropertiesSetOnce: {
    'first_seen': DateTime.now().toIso8601String(),
  },
);
```

### Get Distinct ID

```dart
final distinctId = await Posthog().getDistinctId();
```

### Reset User

```dart
// Clear user data and generate new distinct ID
await Posthog().reset();
```

### Session Management

```dart
// Get current session ID
final sessionId = await Posthog().getSessionId();

// Create a new session (Linux only - useful for kiosk mode)
// This generates a new session ID and sends a session initialization event
await Posthog().createNewSession();
```

**Note**: `createNewSession()` is primarily useful for Linux desktop applications in kiosk mode or robotics scenarios where you want to manually control session boundaries. On Android/iOS, sessions are managed automatically by the native SDK.

---

## Screen Tracking

### Automatic Screen Tracking

Install `PosthogObserver` to automatically track screen views:

```dart
MaterialApp(
  navigatorObservers: [PosthogObserver()],
  // ... your app
)

// Or with GoRouter
final router = GoRouter(
  routes: [/* ... */],
  observers: [PosthogObserver()],
);
```

### Manual Screen Tracking

```dart
await Posthog().screen(
  screenName: 'product_detail',
  properties: {
    'product_id': '123',
    'category': 'electronics',
  },
);
```

### Screen Names in Events

When using `PosthogObserver`, screen names are automatically added to all events via `$screen_name` property.

---

## Best Practices

### 1. Event Naming

Use consistent, descriptive event names:

```dart
// Good
'button_clicked'
'item_added_to_cart'
'checkout_completed'
'user_signed_up'

// Bad
'click'
'action'
'event1'
'btn1'
```

### 2. Property Naming

Use snake_case for property names:

```dart
// Good
properties: {
  'item_id': '123',
  'user_type': 'premium',
  'checkout_amount': 99.99,
}

// Bad
properties: {
  'itemId': '123',  // camelCase
  'user-type': 'premium',  // kebab-case
}
```

### 3. Widget Keys

Use consistent key naming for better autocapture filtering:

```dart
// Use PostHogKeys helper
key: PostHogKeys.button('add_item')
key: PostHogKeys.input('search')
key: PostHogKeys.card('product_123')
```

### 4. Super Properties

Register common properties once:

```dart
// At app startup
await Posthog().register('app_version', '1.2.3');
await Posthog().register('user_role', 'admin');

// These are included in all events automatically
```

### 5. Session Replay Optimization

For apps with idle animations:

```dart
config.sessionReplayConfig.pauseOnIdle = true;
config.sessionReplayConfig.idleTimeout = const Duration(seconds: 5);
```

For kiosk mode / robotics applications:

```dart
config.sessionReplayConfig.pauseOnIdle = true;
config.sessionReplayConfig.idleTimeout = const Duration(seconds: 5);
config.sessionReplayConfig.sessionTimeout = const Duration(minutes: 5);
// New session created automatically if idle period exceeds sessionTimeout
```

For low bandwidth:

```dart
config.sessionReplayConfig.compressionQuality = 50;  // Lower quality
config.sessionReplayConfig.maxImageDimension = 800;  // Resize images
config.sessionReplayConfig.batchSize = 20;  // Larger batches
```

### 6. Error Tracking

Configure stack trace filtering to focus on your code:

```dart
config.errorTrackingConfig.inAppIncludes = ['package:your_app'];
config.errorTrackingConfig.inAppByDefault = true;
```

### 7. Feature Flags

Preload flags for better performance:

```dart
config.preloadFeatureFlags = true;
```

### 8. Batching

Adjust batching for your use case:

```dart
// High-frequency events (games, real-time apps)
config.flushAt = 10;
config.flushInterval = const Duration(seconds: 10);

// Low-frequency events (forms, occasional clicks)
config.flushAt = 20;
config.flushInterval = const Duration(seconds: 30);
```

---

## Multi-Step Flow Tracking

Track complex user flows using the `FlowTracker` mixin:

```dart
import 'package:posthog_flutter/posthog_flutter.dart';

class AddItemScreen extends StatefulWidget {
  @override
  _AddItemScreenState createState() => _AddItemScreenState();
}

class _AddItemScreenState extends State<AddItemScreen> with FlowTracker {
  @override
  void initState() {
    super.initState();
    startFlow('add_item_flow', initialData: {'source': 'home_screen'});
  }

  void _onSearch(String query) {
    trackStep('search', stepData: {'query': query});
  }

  void _onItemSelected(String itemId) {
    trackStep('item_selected', stepData: {'item_id': itemId});
  }

  void _onConfirm() {
    completeFlow(success: true, finalData: {'item_added': true});
  }

  @override
  void dispose() {
    if (currentFlowName != null) {
      abandonFlow();
    }
    super.dispose();
  }
}
```

### Flow Events

The `FlowTracker` mixin automatically sends:
- `flow_started` - When flow begins
- `flow_step` - For each step
- `flow_completed` - When flow finishes successfully
- `flow_abandoned` - When user leaves without completing

### Analyzing Flows in PostHog

Create a funnel in PostHog with events:
1. `flow_started` (filter: `flow_name = 'add_item_flow'`)
2. `flow_step` (filter: `flow_name = 'add_item_flow' AND step = 'search'`)
3. `flow_step` (filter: `flow_name = 'add_item_flow' AND step = 'item_selected'`)
4. `flow_completed` (filter: `flow_name = 'add_item_flow'`)

---

## Troubleshooting

### Events Not Appearing

**Symptoms**: Events don't appear in PostHog dashboard

**Solutions**:
1. **Verify API key**: Check that your API key is correct in `PostHogConfig('your-api-key')`
2. **Check setup order**: Ensure `await Posthog().setup(config)` is called **before** `runApp()`
3. **Enable debug mode**: Set `config.debug = true` and check console logs for:
   - `[PostHog] Sending X events to /capture/`
   - `[PostHog] Sent X events successfully`
4. **Check network**: Verify device has internet connectivity
5. **Verify PostHog project**: Check PostHog project settings and ensure project is active
6. **Check opt-out status**: Verify `config.optOut = false` (or call `Posthog().enable()`)
7. **Wait for flush**: Events are batched - wait up to `flushInterval` seconds or call `Posthog().flush()`

### Session Replay Not Working

**Symptoms**: No session recordings appear in PostHog dashboard

**Solutions**:
1. **Enable in config**: Ensure `config.sessionReplay = true` is set before `setup()`
2. **Wrap app**: Verify `PostHogWidget` wraps your app root (not just a child widget)
3. **PostHog settings**: Check PostHog project settings - Session replay must be enabled in project
4. **Debug logs**: Enable `config.debug = true` and look for:
   - `[Replay] Sending batch: X snapshots`
   - `[Replay] Batch sent successfully`
5. **UI changes**: Session replay only captures when UI actually changes - interact with your app
6. **Platform check**: Verify you're testing on a supported platform (Android, iOS, Linux)
7. **Check session ID**: Ensure events include `$session_id` property (automatic)
8. **Wait for processing**: Recordings may take a few minutes to appear in dashboard

### Autocapture Not Working

**Symptoms**: No `$autocapture` events appear in PostHog dashboard

**Solutions**:
1. **Enable in config**: Ensure `config.autocapture = true` is set before `setup()`
2. **Wrap app**: Verify `PostHogWidget` wraps your app root (it automatically enables autocapture)
3. **Interactive widgets**: Autocapture only tracks taps on interactive widgets (buttons, text fields, etc.)
4. **Debug logs**: Enable `config.debug = true` and look for autocapture event logs
5. **PostHog settings**: Verify PostHog project settings - Autocapture should be enabled
6. **Check events**: Look for events with `event = "$autocapture"` and `properties.$elements` array
7. **Widget keys**: Use `PostHogKeys` helper for better event identification

### Feature Flags Not Loading

1. Check `config.preloadFeatureFlags = true`
2. Verify user is identified: `await Posthog().identify(...)`
3. Check PostHog project settings: Feature flags must be enabled
4. Call `await Posthog().reloadFeatureFlags()` manually if needed

### Surveys Not Showing

1. Ensure `config.surveys = true`
2. Verify `PosthogObserver` is in navigator observers
3. Check PostHog project settings: Surveys must be configured
4. Ensure user is identified (some surveys require identification)

### Too Much Data / High Costs

**Session Replay:**
- Increase `throttleDelay` (capture less frequently)
- Lower `compressionQuality` (smaller files)
- Enable `pauseOnIdle` (skip idle periods)
- Reduce `maxImageDimension` (smaller images)

**Autocapture:**
- Filter events in PostHog dashboard
- Use consistent keys to filter by widget type
- Focus on important screens only

**General:**
- Increase `flushAt` (fewer batches)
- Increase `flushInterval` (less frequent flushing)
- Reduce `maxBatchSize` (smaller batches)

### Performance Issues

**Session Replay:**
- Increase `throttleDelay` to reduce capture frequency
- Enable `pauseOnIdle` to skip idle animations
- Lower `compressionQuality` for faster processing

**Autocapture:**
- Already optimized with caching and hierarchy limiting
- No performance concerns for typical apps

**General:**
- Use async event capture (don't await)
- Batch events appropriately
- Monitor queue size

---

## Platform-Specific Notes

### Linux

- Full feature parity with Android/iOS
- Session replay and autocapture fully supported
- All PostHog features available
- **Additional feature**: `createNewSession()` method for manual session management (useful for kiosk mode)
- **Additional feature**: `sessionTimeout` configuration for automatic session creation after idle periods

### Android

- Surveys supported
- Native exception tracking available
- Full feature set

### iOS

- Surveys supported
- Data mode configuration available (`wifi`, `cellular`, `any`)
- Full feature set

### Web

- Uses JavaScript SDK for most features
- Session replay and autocapture handled by web SDK
- Some features may differ from native implementations

---

## Additional Resources

- [PostHog Documentation](https://posthog.com/docs)
- [PostHog Flutter SDK](https://posthog.com/docs/libraries/flutter)
- [Session Replay Guide](https://posthog.com/docs/session-replay)
- [Feature Flags Guide](https://posthog.com/docs/feature-flags)
- [Surveys Guide](https://posthog.com/docs/surveys)

---

## Summary Checklist

Use this checklist when integrating PostHog into an existing Flutter app:

### Initial Setup
- [ ] Added `posthog_flutter: ^5.9.0` to `pubspec.yaml`
- [ ] Ran `flutter pub get`
- [ ] Called `WidgetsFlutterBinding.ensureInitialized()` before setup
- [ ] Created `PostHogConfig` with your API key
- [ ] Called `await Posthog().setup(config)` **before** `runApp()`
- [ ] Set `config.debug = true` for development (set to `false` for production)

### Widget Integration
- [ ] Wrapped app root with `PostHogWidget` (if using session replay or autocapture)
- [ ] Added `PosthogObserver()` to `navigatorObservers` (for screen tracking and surveys)
- [ ] Verified widget tree structure is correct

### Feature Configuration
- [ ] Configured session replay if needed (`config.sessionReplay = true`)
- [ ] Configured autocapture if needed (`config.autocapture = true`)
- [ ] Configured surveys if needed (`config.surveys = true` - mobile only)
- [ ] Configured error tracking if needed (`config.errorTrackingConfig.*`)
- [ ] Configured feature flags if needed (`config.preloadFeatureFlags = true`)

### Code Integration
- [ ] Implemented user identification (`Posthog().identify()`)
- [ ] Added event tracking calls (`Posthog().capture()`)
- [ ] Added screen tracking (automatic with `PosthogObserver`, or manual `Posthog().screen()`)
- [ ] Used consistent event naming convention (snake_case)
- [ ] Used `PostHogKeys` helper for widget keys (if using autocapture)
- [ ] Registered super properties if needed (`Posthog().register()`)

### Testing & Optimization
- [ ] Tested events appear in PostHog dashboard
- [ ] Tested session replay (if enabled) - verify recordings appear
- [ ] Tested autocapture (if enabled) - verify click events appear
- [ ] Tested feature flags (if used)
- [ ] Optimized session replay settings for your use case (throttle, compression, etc.)
- [ ] Set `config.debug = false` for production builds

### Platform-Specific
- [ ] **Android**: Disabled auto-init in `AndroidManifest.xml` if using manual setup
- [ ] **iOS**: Disabled auto-init in `Info.plist` if using manual setup
- [ ] **Linux**: No additional platform setup required
- [ ] **Web**: Uses JavaScript SDK automatically (no additional setup)
