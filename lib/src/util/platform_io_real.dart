import 'dart:io';

bool isSupportedPlatform() {
  return !Platform.isWindows;
}
