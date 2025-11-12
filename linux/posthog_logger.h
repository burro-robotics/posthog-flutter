#ifndef POSTHOG_LOGGER_H
#define POSTHOG_LOGGER_H

#include <iostream>
#include <string>

enum class LogLevel {
  NONE = 0,
  INFO = 1,
  DEBUG = 2
};

class PostHogLogger {
public:
  static void SetLevel(LogLevel level) {
    log_level_ = level;
  }

  static LogLevel GetLevel() {
    return log_level_;
  }

  // Info level: Production logs - significant events only
  static void Info(const std::string& message) {
    if (log_level_ >= LogLevel::INFO) {
      std::cout << "[PostHog] " << message << std::endl;
    }
  }

  // Debug level: Verbose logs - only in debug mode
  static void Debug(const std::string& message) {
    if (log_level_ >= LogLevel::DEBUG) {
      std::cout << "[PostHog] " << message << std::endl;
    }
  }

  // Error level: Always logged
  static void Error(const std::string& message) {
    std::cerr << "[PostHog] ERROR: " << message << std::endl;
  }

private:
  static LogLevel log_level_;
};

#endif // POSTHOG_LOGGER_H

