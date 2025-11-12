import 'package:logger/logger.dart';
import 'package:posthog_flutter/posthog_flutter.dart';

/// Logger instance for PostHog Flutter
Logger? _logger;

/// Initialize the logger with the debug setting from PostHog config
void _initLogger() {
  if (_logger != null) return;
  
  final config = Posthog().config;
  final isDebug = config?.debug ?? false;
  
  _logger = Logger(
    level: isDebug ? Level.debug : Level.info,
    printer: SimplePrinter(
      colors: false, // Disable colors for cleaner output
      printTime: false, // Don't print timestamps
    ),
  );
}

/// Update logger level based on PostHog config debug flag
void updateLoggerLevel() {
  final config = Posthog().config;
  final isDebug = config?.debug ?? false;
  
  if (_logger != null) {
    // Create a new logger with updated level since level is final
    _logger = Logger(
      level: isDebug ? Level.debug : Level.info,
      printer: SimplePrinter(
        colors: false,
        printTime: false,
      ),
    );
  } else {
    _initLogger();
  }
}

/// Log a debug message (only shown when debug is enabled)
void printIfDebug(String message) {
  _initLogger();
  _logger?.d('[PostHog] $message');
}

/// Log an info message (shown in production)
void logInfo(String message) {
  _initLogger();
  _logger?.i('[PostHog] $message');
}

/// Log an error message (always shown)
void logError(String message) {
  _initLogger();
  _logger?.e('[PostHog] ERROR: $message');
}
