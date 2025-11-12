import 'dart:async';

import 'package:flutter/material.dart';
import 'package:posthog_flutter/posthog_flutter.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Use a test API key - user should replace with their own
  final config =
      PostHogConfig('<YOUR_KEY_HERE>');
  config.debug = true;
  config.captureApplicationLifecycleEvents = false;
  config.host = 'https://us.i.posthog.com';
  config.flushAt = 1; // Flush immediately for testing

  await Posthog().setup(config);

  runApp(const TestApp());
}

class TestApp extends StatelessWidget {
  const TestApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      navigatorObservers: [PosthogObserver()],
      title: 'PostHog Linux Test',
      theme: ThemeData(
        primarySwatch: Colors.blue,
        useMaterial3: true,
      ),
      home: const TestHomePage(),
    );
  }
}

class TestHomePage extends StatefulWidget {
  const TestHomePage({super.key});

  @override
  State<TestHomePage> createState() => _TestHomePageState();
}

class _TestHomePageState extends State<TestHomePage> {
  final _posthog = Posthog();
  String _statusMessage = 'Ready';
  String _distinctId = '';

  @override
  void initState() {
    super.initState();
    _loadDistinctId();
  }

  Future<void> _loadDistinctId() async {
    final id = await _posthog.getDistinctId();
    setState(() {
      _distinctId = id;
    });
  }

  Future<void> _captureEvent(
      String eventName, Map<String, dynamic>? properties) async {
    try {
      await _posthog.capture(
        eventName: eventName,
        properties: properties?.cast<String, Object>(),
      );
      setState(() {
        _statusMessage = 'Event "$eventName" captured successfully!';
      });
    } catch (e) {
      setState(() {
        _statusMessage = 'Error: $e';
      });
    }
  }

  Future<void> _testIdentify() async {
    try {
      await _posthog.identify(
        userId: 'test_user_${DateTime.now().millisecondsSinceEpoch}',
        userProperties: {
          'name': 'Test User',
          'platform': 'Linux',
        } as Map<String, Object>,
      );
      await _loadDistinctId();
      setState(() {
        _statusMessage = 'User identified successfully!';
      });
    } catch (e) {
      setState(() {
        _statusMessage = 'Error: $e';
      });
    }
  }

  Future<void> _testScreen() async {
    try {
      await _posthog.screen(
        screenName: 'test_screen',
        properties: {
          'screen_type': 'test',
        } as Map<String, Object>,
      );
      setState(() {
        _statusMessage = 'Screen tracked successfully!';
      });
    } catch (e) {
      setState(() {
        _statusMessage = 'Error: $e';
      });
    }
  }

  Future<void> _testFlush() async {
    try {
      await _posthog.flush();
      setState(() {
        _statusMessage = 'Events flushed!';
      });
    } catch (e) {
      setState(() {
        _statusMessage = 'Error: $e';
      });
    }
  }

  Future<void> _testFeatureFlag() async {
    try {
      final enabled = await _posthog.isFeatureEnabled('test_flag');
      setState(() {
        _statusMessage = 'Feature flag test_flag: $enabled';
      });
    } catch (e) {
      setState(() {
        _statusMessage = 'Error: $e';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('PostHog Linux Test App'),
        backgroundColor: Colors.blue,
      ),
      body: Container(
        width: 1024,
        height: 600,
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Status section
            Card(
              color: Colors.grey[100],
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Status',
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 8),
                    Text(_statusMessage),
                    const SizedBox(height: 8),
                    Text(
                      'Distinct ID: $_distinctId',
                      style: const TextStyle(fontSize: 12, color: Colors.grey),
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 16),

            // Basic Events section
            const Text(
              'Basic Events',
              style: TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 8),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                ElevatedButton(
                  onPressed: () => _captureEvent(
                      'button_clicked',
                      {
                        'button_name': 'test_button',
                        'timestamp': DateTime.now().toIso8601String(),
                      } as Map<String, dynamic>),
                  child: const Text('Capture Test Event'),
                ),
                ElevatedButton(
                  onPressed: () => _captureEvent(
                      'custom_event',
                      {
                        'property1': 'value1',
                        'property2': 42,
                        'property3': true,
                      } as Map<String, dynamic>),
                  child: const Text('Capture Custom Event'),
                ),
                ElevatedButton(
                  onPressed: _testScreen,
                  child: const Text('Track Screen'),
                ),
                ElevatedButton(
                  onPressed: _testIdentify,
                  child: const Text('Identify User'),
                ),
              ],
            ),
            const SizedBox(height: 16),

            // Feature Flags section
            const Text(
              'Feature Flags',
              style: TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 8),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                ElevatedButton(
                  onPressed: _testFeatureFlag,
                  child: const Text('Check Feature Flag'),
                ),
                ElevatedButton(
                  onPressed: () async {
                    try {
                      await _posthog.reloadFeatureFlags();
                      setState(() {
                        _statusMessage = 'Feature flags reloaded!';
                      });
                    } catch (e) {
                      setState(() {
                        _statusMessage = 'Error: $e';
                      });
                    }
                  },
                  child: const Text('Reload Feature Flags'),
                ),
              ],
            ),
            const SizedBox(height: 16),

            // Utility section
            const Text(
              'Utilities',
              style: TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 8),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                ElevatedButton(
                  onPressed: _testFlush,
                  child: const Text('Flush Events'),
                ),
                ElevatedButton(
                  onPressed: () async {
                    try {
                      await _posthog.register('super_property', 'super_value');
                      setState(() {
                        _statusMessage = 'Super property registered!';
                      });
                    } catch (e) {
                      setState(() {
                        _statusMessage = 'Error: $e';
                      });
                    }
                  },
                  child: const Text('Register Super Property'),
                ),
                ElevatedButton(
                  onPressed: () async {
                    try {
                      await _posthog.reset();
                      await _loadDistinctId();
                      setState(() {
                        _statusMessage = 'Reset completed!';
                      });
                    } catch (e) {
                      setState(() {
                        _statusMessage = 'Error: $e';
                      });
                    }
                  },
                  child: const Text('Reset'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
