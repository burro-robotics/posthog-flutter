import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/gestures.dart';
import 'package:posthog_flutter/posthog_flutter.dart';

/// Widget wrapper that captures tap/click events for PostHog autocapture.
///
/// Wrap your app with this widget to enable autocapture data collection.
/// Autocapture captures element position, widget type, and metadata for each tap.
/// This sends `$autocapture` events with `$elements` array (separate from PostHog's visual heatmap feature, which requires web/iframe).
///
/// Example:
/// ```dart
/// PostHogAutocaptureWidget(
///   child: MaterialApp(
///     home: MyHomePage(),
///   ),
/// )
/// ```
class PostHogAutocaptureWidget extends StatelessWidget {
  final Widget child;

  const PostHogAutocaptureWidget({
    super.key,
    required this.child,
  });

  @override
  Widget build(BuildContext context) {
    // Safely check if Posthog is initialized and autocapture is enabled
    try {
      final posthog = Posthog();
      final config = posthog.config;
      if (config == null || !config.autocapture) {
        return child;
      }
    } catch (e) {
      // Posthog not initialized or disposed, return child without autocapture tracking
      return child;
    }

    return _AutocaptureGestureDetector(child: child);
  }
}

class _AutocaptureGestureDetector extends StatelessWidget {
  final Widget child;

  const _AutocaptureGestureDetector({required this.child});

  void _onTapDown(TapDownDetails details) {
    // Wrap everything in try-catch to prevent crashes
    try {
      // Safely get Posthog instance - it might be disposed
      final posthog = Posthog();
      final config = posthog.config;
      if (config == null || !config.autocapture) {
        return;
      }

      // Check if Posthog platform interface is still valid
      // Accessing _posthog directly could fail if disposed
      try {
        // Try to access a safe property to verify it's still valid
        final _ = posthog.config?.apiKey;
      } catch (e) {
        // Posthog is disposed or invalid, skip this event
        return;
      }

      // Safely get the view
      final views = WidgetsBinding.instance.platformDispatcher.views;
      if (views.isEmpty) {
        return;
      }

      final view = views.first;

      // Perform hit test to find widgets at tap position
      final HitTestResult result = HitTestResult();
      try {
        WidgetsBinding.instance.hitTestInView(
          result,
          details.globalPosition,
          view.viewId,
        );
      } catch (e) {
        // Hit test failed, skip this event
        return;
      }

      // Extract element hierarchy from hit test
      final List<Map<String, dynamic>> elements =
          _extractElementHierarchy(result, details.globalPosition);

      if (elements.isEmpty) {
        return;
      }

      // Get viewport size from the view's physical size
      // Convert from physical pixels to logical pixels
      int viewportWidth = 0;
      int viewportHeight = 0;
      try {
        final double devicePixelRatio = view.devicePixelRatio;
        final Size physicalSize = view.physicalSize;
        if (devicePixelRatio > 0 &&
            physicalSize.width > 0 &&
            physicalSize.height > 0) {
          viewportWidth = (physicalSize.width / devicePixelRatio).toInt();
          viewportHeight = (physicalSize.height / devicePixelRatio).toInt();
        }
      } catch (e) {
        // View size access failed, use defaults
        viewportWidth = 0;
        viewportHeight = 0;
      }

      // Build properties map
      final Map<String, Object> properties = {
        '\$event_type': 'click',
        '\$elements': elements,
        '\$viewport_width': viewportWidth,
        '\$viewport_height': viewportHeight,
      };

      // Add screen name if available
      try {
        final String? screenName = posthog.currentScreen;
        if (screenName != null) {
          properties['\$screen_name'] = screenName;
        }
      } catch (e) {
        // Screen name access failed, continue without it
      }

      // Send autocapture event asynchronously to avoid blocking
      // Don't await to prevent blocking the UI thread
      // Use the same posthog instance we validated earlier
      posthog
          .capture(
        eventName: '\$autocapture',
        properties: properties,
      )
          .catchError((error) {
        // Silently handle errors to prevent crashes
        // In debug mode, you might want to log this
      });
    } catch (e) {
      // Catch any unexpected errors to prevent crashes
      // Silently fail to avoid disrupting the app
    }
  }

  List<Map<String, dynamic>> _extractElementHierarchy(
    HitTestResult result,
    Offset globalPosition,
  ) {
    final List<Map<String, dynamic>> elements = [];

    // Safely traverse hit test entries to build element hierarchy
    // Create a copy of the path to avoid modification during iteration
    final List<HitTestEntry> pathEntries;
    try {
      pathEntries = List<HitTestEntry>.from(result.path);
    } catch (e) {
      // Path access failed, return empty list
      return elements;
    }

    // OPTIMIZATION: Build RenderObject → Widget map once (O(n) traversal)
    // This avoids O(n × m) complexity from searching the tree for each element
    Map<RenderObject, Widget> renderObjectToWidget = {};
    try {
      renderObjectToWidget = _buildRenderObjectToWidgetMap();
    } catch (e) {
      // Map building failed, fall back to per-element lookup (empty map)
    }

    // OPTIMIZATION: Limit hierarchy to last 3-5 elements (most relevant)
    // Take the last elements from the path (closest to tapped widget)
    const int maxHierarchyDepth = 5;
    final int startIndex = pathEntries.length > maxHierarchyDepth
        ? pathEntries.length - maxHierarchyDepth
        : 0;
    final List<HitTestEntry> relevantEntries = pathEntries.sublist(startIndex);

    for (final HitTestEntry entry in relevantEntries) {
      try {
        // Check if target is a RenderObject
        final HitTestTarget target = entry.target;
        if (target is! RenderObject) {
          continue;
        }

        final RenderObject renderObject = target;

        // Check if render object is still attached and valid
        if (!renderObject.attached) {
          continue;
        }

        if (renderObject is! RenderBox) {
          continue;
        }

        final RenderBox box = renderObject;
        if (!box.hasSize) {
          continue;
        }

        // Safely get local position relative to this element
        Offset localPosition;
        try {
          localPosition = box.globalToLocal(globalPosition);
        } catch (e) {
          // Coordinate transformation failed, skip this element
          continue;
        }

        // Check if tap is within bounds
        try {
          if (!box.size.contains(localPosition)) {
            continue;
          }
        } catch (e) {
          // Size check failed, skip this element
          continue;
        }

        final Map<String, dynamic> element = {
          'x': localPosition.dx.toInt(),
          'y': localPosition.dy.toInt(),
          'width': box.size.width.toInt(),
          'height': box.size.height.toInt(),
        };

        // OPTIMIZATION: Use cached map for O(1) lookup instead of O(m) tree traversal
        Widget? widget = renderObjectToWidget[renderObject];

        // Fallback: if not in cache, try the old method (should rarely happen)
        widget ??= _findWidgetForRenderObject(renderObject);

        if (widget != null) {
          try {
            final String widgetType = widget.runtimeType.toString();
            element['tag_name'] = widgetType;

            // Extract widget key
            // Supports ValueKey, ObjectKey, GlobalKey, and other key types
            if (widget.key != null) {
              try {
                final key = widget.key!;
                String? keyValue;

                // Handle different key types
                if (key is ValueKey) {
                  // ValueKey has a value property
                  final value = key.value;
                  if (value is String) {
                    keyValue = value;
                  } else {
                    keyValue = value.toString();
                  }
                } else if (key is ObjectKey) {
                  // ObjectKey has a value property
                  keyValue = key.value.toString();
                } else if (key is GlobalKey) {
                  // GlobalKey - try to get a meaningful identifier
                  // For GlobalKey, we can use the debug label if available
                  final keyStr = key.toString();
                  // Extract from format like "[GlobalKey#abc123]" or "GlobalKey<...>"
                  final match = RegExp(r'\[GlobalKey[^#]*#([a-f0-9]+)\]')
                      .firstMatch(keyStr);
                  if (match != null) {
                    keyValue = 'global_${match.group(1)}';
                  } else {
                    // Fallback: use the hash code
                    keyValue = 'global_${key.hashCode}';
                  }
                } else {
                  // Fallback: try to extract from toString()
                  final String keyStr = key.toString();
                  // Try to extract quoted value (e.g., ValueKey('button1') -> 'button1')
                  final singleQuoteMatch =
                      RegExp(r"'([^']+)'").firstMatch(keyStr);
                  final doubleQuoteMatch =
                      RegExp(r'"([^"]+)"').firstMatch(keyStr);

                  if (singleQuoteMatch != null) {
                    keyValue = singleQuoteMatch.group(1);
                  } else if (doubleQuoteMatch != null) {
                    keyValue = doubleQuoteMatch.group(1);
                  } else {
                    // Use the full string representation
                    keyValue = keyStr;
                  }
                }

                if (keyValue != null && keyValue.isNotEmpty) {
                  element['attr__id'] = keyValue;
                }
              } catch (e) {
                // Key extraction failed, skip it
              }
            }

            // Extract text content
            try {
              final String? text = _extractText(widget);
              if (text != null && text.isNotEmpty) {
                element['text'] = text;
              }
            } catch (e) {
              // Text extraction failed, skip it
            }

            // Extract widget-specific attributes
            try {
              if (widget is Text) {
                element['text'] = widget.data ?? '';
              } else if (widget is Icon) {
                element['text'] = widget.icon.toString();
              } else if (widget is IconButton) {
                if (widget.tooltip != null && widget.tooltip!.isNotEmpty) {
                  element['text'] = widget.tooltip;
                }
              } else if (widget is ButtonStyleButton) {
                // Extract button text if available
                if (widget.child is Text) {
                  element['text'] = (widget.child as Text).data ?? '';
                }
              }
            } catch (e) {
              // Widget-specific attribute extraction failed, skip it
            }
          } catch (e) {
            // Widget processing failed, use fallback
            element['tag_name'] = renderObject.runtimeType.toString();
          }
        } else {
          // Fallback: use render object type
          element['tag_name'] = renderObject.runtimeType.toString();
        }

        elements.add(element);
      } catch (e) {
        // Individual element processing failed, skip it and continue
        continue;
      }
    }

    // Reverse to get parent-to-child order (PostHog expects root to leaf)
    // Note: PostHog web SDK sends elements in root-to-leaf order
    return elements.reversed.toList();
  }

  /// OPTIMIZATION: Builds a RenderObject → Widget map by traversing the element tree once.
  /// This reduces complexity from O(n × m) to O(n) where n = elements in hit path, m = total widgets.
  /// Returns a map that can be used for O(1) lookups instead of O(m) searches per element.
  Map<RenderObject, Widget> _buildRenderObjectToWidgetMap() {
    final Map<RenderObject, Widget> map = {};

    try {
      final rootElement = WidgetsBinding.instance.rootElement;
      if (rootElement == null) {
        return map;
      }

      // Traverse the entire element tree once and build the map
      rootElement.visitChildElements((element) {
        _buildMapFromElement(element, map);
      });
    } catch (e) {
      // Tree traversal failed, return empty map
      // Fallback to per-element lookup will be used
    }

    return map;
  }

  /// Recursively traverses an element and its children to build the RenderObject → Widget map.
  void _buildMapFromElement(Element element, Map<RenderObject, Widget> map) {
    try {
      // Add this element's renderObject → widget mapping
      final renderObject = element.renderObject;
      if (renderObject != null) {
        map[renderObject] = element.widget;
      }

      // Recursively process children
      element.visitChildElements((childElement) {
        _buildMapFromElement(childElement, map);
      });
    } catch (e) {
      // Traversal failed for this branch, continue
    }
  }

  /// Fallback method: Attempts to find the widget associated with a render object by traversing the element tree.
  /// This is only used if the cached map doesn't contain the renderObject (should rarely happen).
  Widget? _findWidgetForRenderObject(RenderObject renderObject) {
    try {
      final rootElement = WidgetsBinding.instance.rootElement;
      if (rootElement == null) {
        return null;
      }

      // Traverse the element tree to find the element with matching renderObject
      Element? foundElement;
      rootElement.visitChildElements((element) {
        if (foundElement != null) return; // Already found, stop searching

        _traverseElementTree(element, renderObject, (matchedElement) {
          foundElement = matchedElement;
        });
      });

      return foundElement?.widget;
    } catch (e) {
      // Element tree traversal failed, return null
      return null;
    }
  }

  /// Recursively traverses the element tree to find an element with matching renderObject.
  /// Used only as a fallback when the cached map doesn't contain the renderObject.
  void _traverseElementTree(
    Element element,
    RenderObject targetRenderObject,
    void Function(Element) onMatch,
  ) {
    try {
      // Check if this element's renderObject matches
      if (element.renderObject == targetRenderObject) {
        onMatch(element);
        return;
      }

      // Recursively check children
      element.visitChildElements((childElement) {
        _traverseElementTree(childElement, targetRenderObject, onMatch);
      });
    } catch (e) {
      // Traversal failed for this branch, continue
    }
  }

  String? _extractText(Widget widget) {
    try {
      if (widget is Text) {
        return widget.data ?? widget.textSpan?.toPlainText();
      } else if (widget is RichText) {
        return widget.text.toPlainText();
      } else if (widget is ButtonStyleButton) {
        if (widget.child is Text) {
          return (widget.child as Text).data;
        }
      }
    } catch (e) {
      // Text extraction failed, return null
    }
    return null;
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTapDown: _onTapDown,
      behavior: HitTestBehavior.translucent,
      child: child,
    );
  }
}
