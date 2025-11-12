import 'dart:async';

import 'package:flutter/material.dart';
import 'package:posthog_flutter/posthog_flutter.dart';
import 'package:posthog_flutter/src/replay/mask/posthog_mask_controller.dart';
import 'package:posthog_flutter/src/util/logging.dart';

import 'replay/change_detector.dart';
import 'replay/native_communicator.dart';
import 'replay/screenshot/screenshot_capturer.dart';

/// Unified PostHog widget that enables features based on configuration.
///
/// This widget automatically enables session replay and/or autocapture based on
/// your PostHogConfig settings. You only need to wrap your app once with this widget.
///
/// Example:
/// ```dart
/// PostHogWidget(
///   child: MaterialApp(
///     home: MyHomePage(),
///   ),
/// )
/// ```
///
/// Features are enabled based on config:
/// - `config.sessionReplay = true` → enables session replay
/// - `config.autocapture = true` → enables autocapture
/// - Both can be enabled simultaneously
class PostHogWidget extends StatefulWidget {
  final Widget child;

  const PostHogWidget({super.key, required this.child});

  @override
  PostHogWidgetState createState() => PostHogWidgetState();
}

class PostHogWidgetState extends State<PostHogWidget> {
  ChangeDetector? _changeDetector;
  ScreenshotCapturer? _screenshotCapturer;
  NativeCommunicator? _nativeCommunicator;

  Timer? _throttleTimer;
  bool _isThrottling = false;
  Duration _throttleDuration = const Duration(milliseconds: 1000);

  // Idle detection state
  Timer? _idleTimer;
  bool _pauseOnIdle = false;
  Duration _idleTimeout = const Duration(seconds: 5);
  Duration _sessionTimeout = const Duration(minutes: 30);
  bool _isIdle = false;
  DateTime? _idleStartTime;

  @override
  void initState() {
    super.initState();

    final config = Posthog().config;
    if (config == null || !config.sessionReplay) {
      return;
    }

    _throttleDuration = config.sessionReplayConfig.throttleDelay;
    _pauseOnIdle = config.sessionReplayConfig.pauseOnIdle;
    _idleTimeout = config.sessionReplayConfig.idleTimeout;
    _sessionTimeout = config.sessionReplayConfig.sessionTimeout;

    _screenshotCapturer = ScreenshotCapturer(config);
    _nativeCommunicator = NativeCommunicator();

    _changeDetector = ChangeDetector(_onChangeDetected);

    // Always start recording immediately
    // If idle detection is enabled, it will pause after idleTimeout
    _changeDetector?.start();

    // If idle detection is enabled, start the idle timer
    if (_pauseOnIdle) {
      _resetIdleTimer();
    }
  }

  /// Resets the idle timer. Called on user interactions.
  void _resetIdleTimer() async {
    _idleTimer?.cancel();

    if (!_pauseOnIdle) {
      return;
    }

    // If we were idle, check if we need to create a new session
    if (_isIdle && _idleStartTime != null) {
      final idleDuration = DateTime.now().difference(_idleStartTime!);
      
      // If idle period exceeded session timeout, create a new session
      if (idleDuration >= _sessionTimeout) {
        logInfo(
            'Idle period (${idleDuration.inMinutes}m) exceeded session timeout (${_sessionTimeout.inMinutes}m), creating new session');
        try {
          await Posthog().createNewSession();
        } catch (e) {
          logInfo('Failed to create new session: $e');
        }
      }
      
      // Reset idle state
      _isIdle = false;
      _idleStartTime = null;
      _changeDetector?.start();
      logInfo('Session recording resumed after user interaction');
    }

    // Start new idle timer
    _idleTimer = Timer(_idleTimeout, () {
      if (mounted && _pauseOnIdle) {
        _isIdle = true;
        _idleStartTime = DateTime.now();
        _changeDetector?.stop();
        logInfo(
            'Session recording paused after ${_idleTimeout.inSeconds}s of inactivity');
      }
    });
  }

  /// Handles user interactions to reset idle timer
  void _onUserInteraction() {
    _resetIdleTimer();
  }

  // This works as onRootViewsChangedListeners
  void _onChangeDetected() {
    if (_isThrottling) {
      // If throttling is active, ignore this call
      return;
    }

    // Start throttling
    _isThrottling = true;

    // Execute the snapshot generation
    _generateSnapshot();

    _throttleTimer?.cancel();
    // Reset throttling after the duration
    _throttleTimer = Timer(_throttleDuration, () {
      _isThrottling = false;
    });
  }

  Future<void> _generateSnapshot() async {
    // Ensure no asynchronous calls occur before this function,
    // as it relies on a consistent state.
    final imageInfo = await _screenshotCapturer?.captureScreenshot();
    if (imageInfo == null) {
      return;
    }

    if (imageInfo.shouldSendMetaEvent) {
      await _nativeCommunicator?.sendMetaEvent(
          width: imageInfo.width,
          height: imageInfo.height,
          screen: Posthog().currentScreen);
    }

    await _nativeCommunicator?.sendFullSnapshot(imageInfo.imageBytes,
        id: imageInfo.id,
        x: imageInfo.x,
        y: imageInfo.y,
        width: imageInfo.width,
        height: imageInfo.height);
  }

  @override
  Widget build(BuildContext context) {
    final config = Posthog().config;
    final needsSessionReplay = config?.sessionReplay ?? false;
    final needsAutocapture = config?.autocapture ?? false;

    // If neither feature is enabled, just return the child
    if (!needsSessionReplay && !needsAutocapture) {
      return widget.child;
    }

    Widget child = widget.child;

    // Wrap with autocapture if enabled (innermost, so it captures taps first)
    if (needsAutocapture) {
      child = PostHogAutocaptureWidget(child: child);
    }

    // Wrap with session replay if enabled (outermost, needs RepaintBoundary)
    if (needsSessionReplay) {
      Widget replayChild = RepaintBoundary(
        key: PostHogMaskController.instance.containerKey,
        child: Column(
          children: [
            Expanded(child: Container(child: child)),
          ],
        ),
      );

      // Wrap with gesture detector to detect all user interactions
      // Only if idle detection is enabled
      if (_pauseOnIdle) {
        replayChild = Listener(
          onPointerDown: (_) => _onUserInteraction(),
          onPointerMove: (_) => _onUserInteraction(),
          onPointerUp: (_) => _onUserInteraction(),
          onPointerCancel: (_) => _onUserInteraction(),
          child: GestureDetector(
            onTap: () => _onUserInteraction(),
            onTapDown: (_) => _onUserInteraction(),
            onTapUp: (_) => _onUserInteraction(),
            onTapCancel: () => _onUserInteraction(),
            onLongPress: () => _onUserInteraction(),
            onLongPressStart: (_) => _onUserInteraction(),
            onLongPressEnd: (_) => _onUserInteraction(),
            onLongPressMoveUpdate: (_) => _onUserInteraction(),
            onLongPressUp: () => _onUserInteraction(),
            // Note: Removed vertical/horizontal drag gestures - scale handles all drags
            onScaleStart: (_) => _onUserInteraction(),
            onScaleUpdate: (_) => _onUserInteraction(),
            onScaleEnd: (_) => _onUserInteraction(),
            onForcePressStart: (_) => _onUserInteraction(),
            onForcePressPeak: (_) => _onUserInteraction(),
            onForcePressUpdate: (_) => _onUserInteraction(),
            onForcePressEnd: (_) => _onUserInteraction(),
            behavior: HitTestBehavior.translucent,
            child: replayChild,
          ),
        );
      }

      child = replayChild;
    }

    return child;
  }

  @override
  void dispose() {
    _throttleTimer?.cancel();
    _throttleTimer = null;
    _idleTimer?.cancel();
    _idleTimer = null;
    _changeDetector?.stop();
    _changeDetector = null;
    _screenshotCapturer = null;
    _nativeCommunicator = null;

    super.dispose();
  }
}
