import 'package:flutter/material.dart';
import 'package:posthog_flutter/posthog_flutter.dart';

/// Mixin for tracking multi-step user flows in PostHog.
///
/// This mixin provides methods to track the start, steps, completion, and
/// abandonment of user flows. It automatically calculates flow duration.
///
/// Example:
/// ```dart
/// class AddItemScreen extends StatefulWidget {
///   @override
///   _AddItemScreenState createState() => _AddItemScreenState();
/// }
///
/// class _AddItemScreenState extends State<AddItemScreen> with FlowTracker {
///   @override
///   void initState() {
///     super.initState();
///     startFlow('add_item_flow', initialData: {'source': 'home_screen'});
///   }
///
///   void _onSearch(String query) {
///     trackStep('search', stepData: {'query': query});
///   }
///
///   void _onConfirm() {
///     completeFlow(success: true, finalData: {'item_added': true});
///   }
///
///   @override
///   void dispose() {
///     if (_flowName != null) {
///       abandonFlow();
///     }
///     super.dispose();
///   }
/// }
/// ```
mixin FlowTracker<T extends StatefulWidget> on State<T> {
  String? _flowName;
  String? _flowStartTime;
  Map<String, dynamic> _flowData = {};

  /// Converts a Map with dynamic values to Map with Object values by filtering out null values
  Map<String, Object> _convertToObjectMap(Map<String, dynamic> map) {
    final result = <String, Object>{};
    for (final entry in map.entries) {
      if (entry.value != null) {
        result[entry.key] = entry.value as Object;
      }
    }
    return result;
  }

  /// Starts tracking a new flow.
  ///
  /// [flowName] should be a unique identifier for this type of flow
  /// (e.g., 'add_item_flow', 'checkout_flow').
  ///
  /// [initialData] is optional metadata to include with all flow events.
  ///
  /// Example:
  /// ```dart
  /// startFlow('add_item_flow', initialData: {'source': 'home_screen'});
  /// ```
  void startFlow(String flowName, {Map<String, dynamic>? initialData}) {
    _flowName = flowName;
    final timestamp = DateTime.now().toIso8601String();
    _flowStartTime = timestamp;
    _flowData = initialData ?? {};

    final Map<String, Object> properties = {
      'flow_name': flowName,
      'timestamp': timestamp,
    };
    properties.addAll(_convertToObjectMap(_flowData));

    Posthog().capture(
      eventName: 'flow_started',
      properties: properties,
    );
  }

  /// Tracks a step within the current flow.
  ///
  /// [stepName] should be a descriptive name for this step
  /// (e.g., 'search', 'item_selected', 'payment_entered').
  ///
  /// [stepData] is optional metadata specific to this step.
  ///
  /// Example:
  /// ```dart
  /// trackStep('search', stepData: {'query': searchQuery});
  /// ```
  void trackStep(String stepName, {Map<String, dynamic>? stepData}) {
    if (_flowName == null) {
      // Flow not started, ignore
      return;
    }

    final Map<String, Object> properties = {
      'flow_name': _flowName!,
      'step': stepName,
      'timestamp': DateTime.now().toIso8601String(),
    };
    properties.addAll(_convertToObjectMap(_flowData));
    if (stepData != null) {
      properties.addAll(_convertToObjectMap(stepData));
    }

    Posthog().capture(
      eventName: 'flow_step',
      properties: properties,
    );
  }

  /// Marks the current flow as completed.
  ///
  /// Automatically calculates and includes flow duration.
  ///
  /// [success] indicates whether the flow completed successfully.
  /// [finalData] is optional metadata to include with the completion event.
  ///
  /// Example:
  /// ```dart
  /// completeFlow(success: true, finalData: {'item_added': true});
  /// ```
  void completeFlow({bool success = true, Map<String, dynamic>? finalData}) {
    if (_flowName == null) {
      // Flow not started, ignore
      return;
    }

    final duration = _flowStartTime != null
        ? DateTime.now()
            .difference(DateTime.parse(_flowStartTime!))
            .inSeconds
        : null;

    final Map<String, Object> properties = {
      'flow_name': _flowName!,
      'success': success,
      'timestamp': DateTime.now().toIso8601String(),
    };
    if (duration != null) {
      properties['duration_seconds'] = duration;
    }
    properties.addAll(_convertToObjectMap(_flowData));
    if (finalData != null) {
      properties.addAll(_convertToObjectMap(finalData));
    }

    Posthog().capture(
      eventName: 'flow_completed',
      properties: properties,
    );

    _resetFlow();
  }

  /// Marks the current flow as abandoned.
  ///
  /// Automatically calculates and includes flow duration.
  /// Use this when the user leaves the flow without completing it.
  ///
  /// [abandonData] is optional metadata to include with the abandonment event.
  ///
  /// Example:
  /// ```dart
  /// abandonFlow(abandonData: {'last_step': 'search'});
  /// ```
  void abandonFlow({Map<String, dynamic>? abandonData}) {
    if (_flowName == null) {
      // Flow not started, ignore
      return;
    }

    final duration = _flowStartTime != null
        ? DateTime.now()
            .difference(DateTime.parse(_flowStartTime!))
            .inSeconds
        : null;

    final Map<String, Object> properties = {
      'flow_name': _flowName!,
      'timestamp': DateTime.now().toIso8601String(),
    };
    if (duration != null) {
      properties['duration_seconds'] = duration;
    }
    properties.addAll(_convertToObjectMap(_flowData));
    if (abandonData != null) {
      properties.addAll(_convertToObjectMap(abandonData));
    }

    Posthog().capture(
      eventName: 'flow_abandoned',
      properties: properties,
    );

    _resetFlow();
  }

  /// Gets the current flow name, or null if no flow is active.
  String? get currentFlowName => _flowName;

  /// Gets the current flow data.
  Map<String, dynamic> get currentFlowData => Map.unmodifiable(_flowData);

  /// Updates the flow data with new values.
  ///
  /// Merges [newData] into the existing flow data.
  ///
  /// Example:
  /// ```dart
  /// updateFlowData({'selected_item': itemId});
  /// ```
  void updateFlowData(Map<String, dynamic> newData) {
    _flowData.addAll(newData);
  }

  void _resetFlow() {
    _flowName = null;
    _flowStartTime = null;
    _flowData = {};
  }
}
