#include "posthog_flutter_plugin.h"
#include "storage_manager.h"
#include "http_client.h"
#include "feature_flags_manager.h"
#include "session_replay_manager.h"
#include "posthog_models.h"
#include "posthog_logger.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>
#include <ctime>
#include <mutex>

using json = nlohmann::json;

#define POSTHOG_FLUTTER_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), posthog_flutter_plugin_get_type(), \
                              PosthogFlutterPlugin))

// Helper function to get current timestamp in milliseconds since epoch
static int64_t get_current_timestamp_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

// Helper function to convert FlValue to nlohmann::json object
static json fl_value_to_json_obj(FlValue* value) {
  if (!value) return json();
  
  FlValueType type = fl_value_get_type(value);
  
  switch (type) {
    case FL_VALUE_TYPE_NULL:
      return json();
    case FL_VALUE_TYPE_BOOL:
      return json(fl_value_get_bool(value));
    case FL_VALUE_TYPE_INT:
      return json(fl_value_get_int(value));
    case FL_VALUE_TYPE_FLOAT:
      return json(fl_value_get_float(value));
    case FL_VALUE_TYPE_STRING:
      return json(fl_value_get_string(value));
    case FL_VALUE_TYPE_UINT8_LIST: {
      // For byte arrays, return as string representation
      return json("<binary>");
    }
    case FL_VALUE_TYPE_LIST: {
      json arr = json::array();
      size_t length = fl_value_get_length(value);
      for (size_t i = 0; i < length; i++) {
        arr.push_back(fl_value_to_json_obj(fl_value_get_list_value(value, i)));
      }
      return arr;
    }
    case FL_VALUE_TYPE_MAP: {
      // Flutter Linux FlValue API doesn't provide a way to enumerate map keys.
      // Properties are handled manually in handle_capture() by checking for known keys.
      // This is a limitation of the Flutter Linux platform channel API.
      return json::object();
    }
    default:
      return json("<unknown>");
  }
}


// Complete struct definition (matches header forward declaration)
struct _PosthogFlutterPlugin {
  GObject parent_instance;
  
  FlMethodChannel* channel;
  StorageManager* storage_manager;
  HttpClient* http_client;
  FeatureFlagsManager* feature_flags_manager;
  SessionReplayManager* session_replay_manager;
  
  std::string api_key;
  std::string host;
  int flush_at;
  int max_queue_size;
  int max_batch_size;
  int flush_interval_seconds;
  bool debug;
  bool opt_out;
  bool initialized;
  bool session_replay_enabled;
  
  std::thread flush_thread;
  bool should_flush;
  std::mutex config_mutex;
};

// Class struct definition (must be before G_DEFINE_TYPE)
struct _PosthogFlutterPluginClass {
  GObjectClass parent_class;
};

G_DEFINE_TYPE(PosthogFlutterPlugin, posthog_flutter_plugin, g_object_get_type())

// Forward declarations
static void handle_method_call(FlMethodChannel* channel, FlMethodCall* method_call,
                               gpointer user_data);
static void posthog_flutter_plugin_dispose(GObject* object);
static std::string get_app_data_dir();
static std::string generate_uuid();
static std::string get_or_create_distinct_id(StorageManager* storage);
static std::string get_or_create_session_id(StorageManager* storage);
static void flush_events_thread(PosthogFlutterPlugin* plugin);

// Helper function to get app data directory
static std::string get_app_data_dir() {
  const char* home = getenv("HOME");
  if (!home) {
    struct passwd* pw = getpwuid(getuid());
    if (pw) {
      home = pw->pw_dir;
    }
  }
  
  if (!home) {
    return "/tmp/posthog_flutter";
  }
  
  std::string dir = std::string(home) + "/.local/share/posthog_flutter";
  return dir;
}

// Generate UUID v4
static std::string generate_uuid() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);
  std::uniform_int_distribution<> dis2(8, 11);

  std::ostringstream oss;
  oss << std::hex;
  for (int i = 0; i < 32; i++) {
    if (i == 8 || i == 12 || i == 16 || i == 20) {
      oss << "-";
    }
    if (i == 12) {
      oss << "4";
    } else if (i == 16) {
      oss << dis2(gen);
    } else {
      oss << dis(gen);
    }
  }
  return oss.str();
}

static std::string get_or_create_distinct_id(StorageManager* storage) {
  std::string distinct_id = storage->GetDistinctId();
  if (distinct_id.empty()) {
    distinct_id = generate_uuid();
    storage->SetDistinctId(distinct_id);
  }
  return distinct_id;
}

static std::string get_or_create_session_id(StorageManager* storage) {
  std::string session_id = storage->GetSessionId();
  if (session_id.empty()) {
    session_id = generate_uuid();
    storage->SetSessionId(session_id);
  }
  return session_id;
}

// Flush events in background thread
static void flush_events_thread(PosthogFlutterPlugin* plugin) {
  while (plugin->should_flush) {
    std::this_thread::sleep_for(std::chrono::seconds(plugin->flush_interval_seconds));
    
    if (!plugin->should_flush) break;
    
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    
    // CRITICAL: Check pointers before use to prevent segfaults
    // These can become null if plugin is being disposed
    if (!plugin->storage_manager || !plugin->http_client) {
      // Pointers are null (plugin being disposed), exit thread
      break;
    }
    
    if (plugin->opt_out || !plugin->initialized) {
      continue;
    }
    
    try {
      std::vector<std::string> events = plugin->storage_manager->GetQueuedEvents(plugin->max_batch_size);
      if (events.empty()) {
        continue;
      }
      
      std::vector<std::string> event_jsons;
      std::vector<std::string> event_ids;
      
      for (const auto& event_with_id : events) {
        size_t pos = event_with_id.find('|');
        if (pos != std::string::npos) {
          event_ids.push_back(event_with_id.substr(0, pos));
          event_jsons.push_back(event_with_id.substr(pos + 1));
        }
      }
      
      if (!event_jsons.empty()) {
        // Double-check pointers are still valid after processing
        if (!plugin->http_client || !plugin->storage_manager) {
          break;
        }
        
        HttpResponse response = plugin->http_client->PostCapture(event_jsons);
        if (response.success && plugin->storage_manager) {
          plugin->storage_manager->RemoveEvents(event_ids);
        }
      }
    } catch (const std::exception& e) {
      PostHogLogger::Error("Error in flush thread: " + std::string(e.what()));
      // Continue loop - don't crash the thread
    } catch (...) {
      PostHogLogger::Error("Unknown error in flush thread");
      // Continue loop - don't crash the thread
    }
  }
}

static void posthog_flutter_plugin_dispose(GObject* object) {
  PosthogFlutterPlugin* plugin = POSTHOG_FLUTTER_PLUGIN(object);
  
  {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    plugin->should_flush = false;
  }
  
  // CRITICAL: Stop session replay manager FIRST, before stopping main flush thread
  // This ensures its background thread stops before we delete http_client/storage_manager
  if (plugin->session_replay_manager) {
    plugin->session_replay_manager->SetActive(false);
    plugin->session_replay_manager->Flush();
    delete plugin->session_replay_manager;
    plugin->session_replay_manager = nullptr;
  }
  
  // Now wait for main flush thread to finish
  if (plugin->flush_thread.joinable()) {
    plugin->flush_thread.join();
  }
  
  // Now safe to delete storage_manager and http_client
  // (session_replay_manager background thread is stopped)
  if (plugin->storage_manager) {
    plugin->storage_manager->Close();
    delete plugin->storage_manager;
    plugin->storage_manager = nullptr;
  }
  
  if (plugin->http_client) {
    delete plugin->http_client;
    plugin->http_client = nullptr;
  }
  
  if (plugin->feature_flags_manager) {
    delete plugin->feature_flags_manager;
    plugin->feature_flags_manager = nullptr;
  }
  
  g_clear_object(&plugin->channel);
  
  G_OBJECT_CLASS(posthog_flutter_plugin_parent_class)->dispose(object);
}

static void posthog_flutter_plugin_class_init(PosthogFlutterPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = posthog_flutter_plugin_dispose;
}

static void posthog_flutter_plugin_init(PosthogFlutterPlugin* self) {
  self->channel = nullptr;
  self->storage_manager = nullptr;
  self->http_client = nullptr;
  self->feature_flags_manager = nullptr;
  self->session_replay_manager = nullptr;
  self->initialized = false;
  self->should_flush = false;
  self->session_replay_enabled = false;
  self->flush_at = 20;
  self->max_queue_size = 1000;
  self->max_batch_size = 50;
  self->flush_interval_seconds = 30;
  self->debug = false;
  self->opt_out = false;
}

// Handle setup method
static void handle_setup(PosthogFlutterPlugin* plugin, FlValue* args) {
  if (plugin->initialized) {
    return;
  }
  
  if (fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
    return;
  }
  
  FlValue* api_key_value = fl_value_lookup_string(args, "apiKey");
  if (!api_key_value || fl_value_get_type(api_key_value) != FL_VALUE_TYPE_STRING) {
    return;
  }
  
  plugin->api_key = fl_value_get_string(api_key_value);
  // Guard against empty API key to prevent crashes in downstream components.
  if (plugin->api_key.empty()) {
    PostHogLogger::Error("PostHog setup called with empty API key. Skipping initialization.");
    return;
  }
  
  FlValue* host_value = fl_value_lookup_string(args, "host");
  if (host_value && fl_value_get_type(host_value) == FL_VALUE_TYPE_STRING) {
    plugin->host = fl_value_get_string(host_value);
  } else {
    plugin->host = "https://us.i.posthog.com";
  }
  if (plugin->host.empty()) {
    plugin->host = "https://us.i.posthog.com";
  }
  
  FlValue* flush_at_value = fl_value_lookup_string(args, "flushAt");
  if (flush_at_value && fl_value_get_type(flush_at_value) == FL_VALUE_TYPE_INT) {
    plugin->flush_at = fl_value_get_int(flush_at_value);
  }
  
  FlValue* max_queue_size_value = fl_value_lookup_string(args, "maxQueueSize");
  if (max_queue_size_value && fl_value_get_type(max_queue_size_value) == FL_VALUE_TYPE_INT) {
    plugin->max_queue_size = fl_value_get_int(max_queue_size_value);
  }
  
  FlValue* max_batch_size_value = fl_value_lookup_string(args, "maxBatchSize");
  if (max_batch_size_value && fl_value_get_type(max_batch_size_value) == FL_VALUE_TYPE_INT) {
    plugin->max_batch_size = fl_value_get_int(max_batch_size_value);
  }
  
  FlValue* flush_interval_value = fl_value_lookup_string(args, "flushInterval");
  if (flush_interval_value && fl_value_get_type(flush_interval_value) == FL_VALUE_TYPE_INT) {
    plugin->flush_interval_seconds = fl_value_get_int(flush_interval_value);
  }
  
  FlValue* debug_value = fl_value_lookup_string(args, "debug");
  if (debug_value && fl_value_get_type(debug_value) == FL_VALUE_TYPE_BOOL) {
    plugin->debug = fl_value_get_bool(debug_value);
    // Set log level based on debug flag
    PostHogLogger::SetLevel(plugin->debug ? LogLevel::DEBUG : LogLevel::INFO);
  }
  
  FlValue* opt_out_value = fl_value_lookup_string(args, "optOut");
  if (opt_out_value && fl_value_get_type(opt_out_value) == FL_VALUE_TYPE_BOOL) {
    plugin->opt_out = fl_value_get_bool(opt_out_value);
  }
  
  // Initialize storage
  std::string app_data_dir = get_app_data_dir();
  plugin->storage_manager = new StorageManager();
  if (!plugin->storage_manager->Initialize(app_data_dir)) {
    PostHogLogger::Error("Failed to initialize storage");
    delete plugin->storage_manager;
    plugin->storage_manager = nullptr;
    return;
  }
  
  // Initialize HTTP client
  plugin->http_client = new HttpClient();
  if (!plugin->http_client->Initialize()) {
    PostHogLogger::Error("Failed to initialize HTTP client");
    delete plugin->http_client;
    plugin->http_client = nullptr;
    return;
  }
  
  plugin->http_client->SetBaseUrl(plugin->host);
  plugin->http_client->SetApiKey(plugin->api_key);
  plugin->http_client->SetDebug(plugin->debug);
  
  // Initialize feature flags manager
  plugin->feature_flags_manager = new FeatureFlagsManager(plugin->http_client, plugin->storage_manager);
  
  // Initialize session replay manager if enabled
  FlValue* session_replay_value = fl_value_lookup_string(args, "sessionReplay");
  bool session_replay = false;
  if (session_replay_value && fl_value_get_type(session_replay_value) == FL_VALUE_TYPE_BOOL) {
    session_replay = fl_value_get_bool(session_replay_value);
  }
  
  plugin->session_replay_enabled = session_replay;
  if (session_replay) {
    PostHogLogger::Debug("Initializing session replay...");
    plugin->session_replay_manager = new SessionReplayManager(plugin->http_client, plugin->storage_manager, plugin->api_key);
    plugin->session_replay_manager->SetActive(true);
    plugin->session_replay_manager->SetDebug(plugin->debug);
    
    // Configure session replay settings
    FlValue* replay_config_value = fl_value_lookup_string(args, "sessionReplayConfig");
    if (replay_config_value && fl_value_get_type(replay_config_value) == FL_VALUE_TYPE_MAP) {
      FlValue* quality_value = fl_value_lookup_string(replay_config_value, "compressionQuality");
      if (quality_value && fl_value_get_type(quality_value) == FL_VALUE_TYPE_INT) {
        plugin->session_replay_manager->SetCompressionQuality(fl_value_get_int(quality_value));
      }
      
      FlValue* batch_size_value = fl_value_lookup_string(replay_config_value, "batchSize");
      if (batch_size_value && fl_value_get_type(batch_size_value) == FL_VALUE_TYPE_INT) {
        plugin->session_replay_manager->SetBatchSize(fl_value_get_int(batch_size_value));
      }
      
      FlValue* batch_interval_value = fl_value_lookup_string(replay_config_value, "batchIntervalMs");
      if (batch_interval_value && fl_value_get_type(batch_interval_value) == FL_VALUE_TYPE_INT) {
        plugin->session_replay_manager->SetBatchInterval(fl_value_get_int(batch_interval_value));
      }
      
      FlValue* max_dim_value = fl_value_lookup_string(replay_config_value, "maxImageDimension");
      if (max_dim_value && fl_value_get_type(max_dim_value) == FL_VALUE_TYPE_INT) {
        plugin->session_replay_manager->SetMaxImageDimension(fl_value_get_int(max_dim_value));
      }
    }
  }
  
  // Set opt-out state
  plugin->storage_manager->SetOptOut(plugin->opt_out);
  
  // Get or create distinct ID
  std::string distinct_id = get_or_create_distinct_id(plugin->storage_manager);
  
  // Generate a new session ID each time the app starts (don't persist across app restarts)
  std::string session_id = generate_uuid();
  plugin->storage_manager->SetSessionId(session_id);
  
  // Preload feature flags if enabled
  FlValue* preload_flags_value = fl_value_lookup_string(args, "preloadFeatureFlags");
  bool preload_flags = true;
  if (preload_flags_value && fl_value_get_type(preload_flags_value) == FL_VALUE_TYPE_BOOL) {
    preload_flags = fl_value_get_bool(preload_flags_value);
  }
  
  if (preload_flags && !plugin->opt_out) {
    std::map<std::string, std::string> properties;
    plugin->feature_flags_manager->ReloadFeatureFlags(distinct_id, properties);
  }
  
  plugin->initialized = true;
  plugin->should_flush = true;
  plugin->flush_thread = std::thread(flush_events_thread, plugin);
  
  // Automatically send session initialization event to establish session context
  // This ensures PostHog recognizes the session and can link snapshot events
  posthog::PostHogEvent init_event;
  init_event.event = "$screen";
  init_event.distinct_id = distinct_id;
  init_event.timestamp = get_current_timestamp_ms();
  init_event.properties["$screen_name"] = "App Started";
  init_event.properties["$session_id"] = session_id;
  init_event.properties["$window_id"] = "main";
  init_event.properties["$lib"] = "posthog-flutter";
  init_event.properties["$lib_version"] = "5.9.0";
  init_event.properties["$device_type"] = "Mobile";
  init_event.properties["$os"] = "Linux";
  
  json init_event_json = init_event.to_json();
  plugin->storage_manager->EnqueueEvent(init_event_json.dump());
  
  PostHogLogger::Debug("Session initialized with session_id: " + session_id);
  
  // Don't log API key for security - initialization is implicit
}

// Handle capture method
static void handle_capture(PosthogFlutterPlugin* plugin, FlValue* args) {
  std::lock_guard<std::mutex> lock(plugin->config_mutex);
  
  if (!plugin->initialized || plugin->opt_out) {
    return;
  }
  
  FlValue* event_name_value = fl_value_lookup_string(args, "eventName");
  if (!event_name_value || fl_value_get_type(event_name_value) != FL_VALUE_TYPE_STRING) {
    return;
  }
  
  std::string event_name = fl_value_get_string(event_name_value);
  
  // Build PostHog event using structs
  posthog::PostHogEvent event;
  event.event = event_name;
  event.distinct_id = get_or_create_distinct_id(plugin->storage_manager);
  event.timestamp = get_current_timestamp_ms();
  
  // Build properties JSON object
  json properties = json::object();
  
  // Add required PostHog library properties
  properties["$lib"] = "posthog-flutter";
  properties["$lib_version"] = "5.9.0";
  properties["$device_type"] = "Mobile";
  properties["$os"] = "Linux";
  properties["$os_version"] = "Unknown";
  properties["$screen_width"] = 1024;
  properties["$screen_height"] = 600;
  
  // Add session_id to link events to session replay
  std::string session_id = get_or_create_session_id(plugin->storage_manager);
  if (!session_id.empty()) {
    properties["$session_id"] = session_id;
  }
  
  // Add window_id to match session replay events
  properties["$window_id"] = "main";
  
  // Add super properties
  auto super_props = plugin->storage_manager->GetAllSuperProperties();
  for (const auto& prop : super_props) {
    try {
      // Parse the JSON value string
      json prop_value = json::parse(prop.second);
      properties[prop.first] = prop_value;
    } catch (const json::exception&) {
      // If parsing fails, try as string
      properties[prop.first] = prop.second;
    }
  }
  
  // Add event properties from args
  // Note: Flutter Linux FlValue API doesn't provide a way to enumerate map keys,
  // so we manually extract known properties (especially $elements for autocapture)
  FlValue* properties_value = fl_value_lookup_string(args, "properties");
  if (properties_value && fl_value_get_type(properties_value) == FL_VALUE_TYPE_MAP) {
    // Manually extract $elements (autocapture) - this is critical for autocapture to work
    FlValue* elements_value = fl_value_lookup_string(properties_value, "$elements");
    if (elements_value && fl_value_get_type(elements_value) == FL_VALUE_TYPE_LIST) {
      properties["$elements"] = fl_value_to_json_obj(elements_value);
    }
    
    // Extract other common autocapture properties
    FlValue* event_type_value = fl_value_lookup_string(properties_value, "$event_type");
    if (event_type_value && fl_value_get_type(event_type_value) == FL_VALUE_TYPE_STRING) {
      properties["$event_type"] = fl_value_get_string(event_type_value);
    }
    
    FlValue* viewport_width_value = fl_value_lookup_string(properties_value, "$viewport_width");
    if (viewport_width_value && fl_value_get_type(viewport_width_value) == FL_VALUE_TYPE_INT) {
      properties["$viewport_width"] = fl_value_get_int(viewport_width_value);
    }
    
    FlValue* viewport_height_value = fl_value_lookup_string(properties_value, "$viewport_height");
    if (viewport_height_value && fl_value_get_type(viewport_height_value) == FL_VALUE_TYPE_INT) {
      properties["$viewport_height"] = fl_value_get_int(viewport_height_value);
    }
    
    // Extract screen_name if present
    FlValue* screen_name_value = fl_value_lookup_string(properties_value, "$screen_name");
    if (screen_name_value && fl_value_get_type(screen_name_value) == FL_VALUE_TYPE_STRING) {
      properties["$screen_name"] = fl_value_get_string(screen_name_value);
    }
    
    // Try to extract any other properties using the generic converter
    // This handles nested maps/arrays that we might not know about
    json event_props = fl_value_to_json_obj(properties_value);
    if (event_props.is_object()) {
      // Merge any additional properties that were successfully converted
      for (auto& [key, value] : event_props.items()) {
        // Only add if we haven't already manually extracted it
        if (properties.find(key) == properties.end()) {
          properties[key] = value;
        }
      }
    }
  }
  
  event.properties = properties;
  
  // Convert to JSON string for storage
  json event_json_obj = event.to_json();
  std::string event_json_str = event_json_obj.dump();
  
  // Sanitize for logging: remove API key if present, truncate if too long
  std::string sanitized_json = event_json_str;
  // Remove API key from JSON string if present
  size_t api_key_pos = sanitized_json.find("\"api_key\"");
  if (api_key_pos != std::string::npos) {
    size_t start = sanitized_json.find('"', api_key_pos + 9);
    size_t end = sanitized_json.find('"', start + 1);
    if (end != std::string::npos) {
      sanitized_json.replace(start + 1, end - start - 1, "***");
    }
  }
  // Truncate if too long (show first and last 40 chars)
  if (sanitized_json.length() > 80) {
    sanitized_json = sanitized_json.substr(0, 40) + "..." + sanitized_json.substr(sanitized_json.length() - 40);
  }
  PostHogLogger::Debug("Event JSON: " + sanitized_json);
  
  // Enqueue event
  plugin->storage_manager->EnqueueEvent(event_json_str);
  
  // Check if we should flush
  int queue_size = plugin->storage_manager->GetQueueSize();
  
  if (queue_size >= plugin->flush_at) {
    std::vector<std::string> events = plugin->storage_manager->GetQueuedEvents(plugin->max_batch_size);
    std::vector<std::string> event_jsons;
    std::vector<std::string> event_ids;
    
    for (const auto& event_with_id : events) {
      size_t pos = event_with_id.find('|');
      if (pos != std::string::npos) {
        event_ids.push_back(event_with_id.substr(0, pos));
        event_jsons.push_back(event_with_id.substr(pos + 1));
      }
    }
    
    if (!event_jsons.empty()) {
      HttpResponse response = plugin->http_client->PostCapture(event_jsons);
      
      // Only log errors - success is silent in production
      if (!response.success) {
        PostHogLogger::Error("Failed to send " + std::to_string(event_jsons.size()) + " events: HTTP " + std::to_string(response.status_code));
        if (!response.body.empty()) {
          PostHogLogger::Error("Response body: " + response.body);
        }
      }
      
      if (response.success) {
        plugin->storage_manager->RemoveEvents(event_ids);
      }
    }
  }
}

// Handle identify method
static void handle_identify(PosthogFlutterPlugin* plugin, FlValue* args) {
  std::lock_guard<std::mutex> lock(plugin->config_mutex);
  
  if (!plugin->initialized || plugin->opt_out) {
    return;
  }
  
  FlValue* user_id_value = fl_value_lookup_string(args, "userId");
  if (!user_id_value || fl_value_get_type(user_id_value) != FL_VALUE_TYPE_STRING) {
    return;
  }
  
  std::string user_id = fl_value_get_string(user_id_value);
  plugin->storage_manager->SetDistinctId(user_id);
  
  // Capture identify event using structs
  posthog::PostHogEvent event;
  event.event = "$identify";
  event.distinct_id = user_id;
  event.timestamp = get_current_timestamp_ms();
  event.properties = json::object();
  
  // Add session_id to link events to session replay
  std::string session_id = get_or_create_session_id(plugin->storage_manager);
  if (!session_id.empty()) {
    event.properties["$session_id"] = session_id;
  }
  
  // Add window_id to match session replay events
  event.properties["$window_id"] = "main";
  
  json event_json_obj = event.to_json();
  plugin->storage_manager->EnqueueEvent(event_json_obj.dump());
}

// Handle screen method
static void handle_screen(PosthogFlutterPlugin* plugin, FlValue* args) {
  std::lock_guard<std::mutex> lock(plugin->config_mutex);
  
  if (!plugin->initialized || plugin->opt_out) {
    return;
  }
  
  FlValue* screen_name_value = fl_value_lookup_string(args, "screenName");
  if (!screen_name_value || fl_value_get_type(screen_name_value) != FL_VALUE_TYPE_STRING) {
    return;
  }
  
  std::string screen_name = fl_value_get_string(screen_name_value);
  
  // Build screen event using structs
  posthog::PostHogEvent event;
  event.event = "$screen";
  event.distinct_id = get_or_create_distinct_id(plugin->storage_manager);
  event.timestamp = get_current_timestamp_ms();
  event.properties["$screen_name"] = screen_name;
  
  // Add required PostHog library properties
  event.properties["$lib"] = "posthog-flutter";
  event.properties["$lib_version"] = "5.9.0";
  event.properties["$device_type"] = "Mobile";
  event.properties["$os"] = "Linux";
  event.properties["$os_version"] = "Unknown";
  event.properties["$screen_width"] = 1024;
  event.properties["$screen_height"] = 600;
  
  // Add session_id to link events to session replay
  std::string session_id = get_or_create_session_id(plugin->storage_manager);
  if (!session_id.empty()) {
    event.properties["$session_id"] = session_id;
  }
  
  // Add window_id to match session replay events
  event.properties["$window_id"] = "main";
  
  json event_json_obj = event.to_json();
  plugin->storage_manager->EnqueueEvent(event_json_obj.dump());
}

// Handle other methods
static void handle_method_call(FlMethodChannel* channel, FlMethodCall* method_call,
                               gpointer user_data) {
  PosthogFlutterPlugin* plugin = POSTHOG_FLUTTER_PLUGIN(user_data);
  
  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* args = fl_method_call_get_args(method_call);
  
  if (strcmp(method, "setup") == 0) {
    handle_setup(plugin, args);
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "capture") == 0) {
    handle_capture(plugin, args);
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "identify") == 0) {
    handle_identify(plugin, args);
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "screen") == 0) {
    handle_screen(plugin, args);
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "distinctId") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    std::string distinct_id = plugin->storage_manager ? 
      get_or_create_distinct_id(plugin->storage_manager) : "";
    g_autoptr(FlValue) result = fl_value_new_string(distinct_id.c_str());
    fl_method_call_respond_success(method_call, result, nullptr);
  } else if (strcmp(method, "reset") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    if (plugin->storage_manager) {
      std::string new_id = generate_uuid();
      plugin->storage_manager->SetDistinctId(new_id);
      // Clear super properties
      auto super_props = plugin->storage_manager->GetAllSuperProperties();
      for (const auto& prop : super_props) {
        plugin->storage_manager->RemoveSuperProperty(prop.first);
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "enable") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    plugin->opt_out = false;
    if (plugin->storage_manager) {
      plugin->storage_manager->SetOptOut(false);
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "disable") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    plugin->opt_out = true;
    if (plugin->storage_manager) {
      plugin->storage_manager->SetOptOut(true);
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "isOptOut") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    bool opt_out = plugin->opt_out;
    if (plugin->storage_manager) {
      opt_out = plugin->storage_manager->GetOptOut();
    }
    g_autoptr(FlValue) result = fl_value_new_bool(opt_out);
    fl_method_call_respond_success(method_call, result, nullptr);
  } else if (strcmp(method, "debug") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* debug_value = fl_value_lookup_string(args, "debug");
      if (debug_value && fl_value_get_type(debug_value) == FL_VALUE_TYPE_BOOL) {
        std::lock_guard<std::mutex> lock(plugin->config_mutex);
        plugin->debug = fl_value_get_bool(debug_value);
        if (plugin->http_client) {
          plugin->http_client->SetDebug(plugin->debug);
        }
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "register") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* key_value = fl_value_lookup_string(args, "key");
      FlValue* value_value = fl_value_lookup_string(args, "value");
      if (key_value && value_value) {
        std::lock_guard<std::mutex> lock(plugin->config_mutex);
        if (plugin->storage_manager) {
          // Convert value to JSON string (simplified)
          std::string value_json = "\"" + std::string(fl_value_get_string(value_value)) + "\"";
          plugin->storage_manager->SetSuperProperty(
            fl_value_get_string(key_value), value_json);
        }
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "unregister") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* key_value = fl_value_lookup_string(args, "key");
      if (key_value) {
        std::lock_guard<std::mutex> lock(plugin->config_mutex);
        if (plugin->storage_manager) {
          plugin->storage_manager->RemoveSuperProperty(fl_value_get_string(key_value));
        }
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "flush") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    if (plugin->initialized && !plugin->opt_out && plugin->storage_manager) {
      std::vector<std::string> events = plugin->storage_manager->GetQueuedEvents(plugin->max_batch_size);
      std::vector<std::string> event_jsons;
      std::vector<std::string> event_ids;
      
      for (const auto& event_with_id : events) {
        size_t pos = event_with_id.find('|');
        if (pos != std::string::npos) {
          event_ids.push_back(event_with_id.substr(0, pos));
          event_jsons.push_back(event_with_id.substr(pos + 1));
        }
      }
      
      if (!event_jsons.empty() && plugin->http_client) {
        HttpResponse response = plugin->http_client->PostCapture(event_jsons);
        if (response.success) {
          plugin->storage_manager->RemoveEvents(event_ids);
        }
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "isFeatureEnabled") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* key_value = fl_value_lookup_string(args, "key");
      if (key_value) {
        std::lock_guard<std::mutex> lock(plugin->config_mutex);
        bool enabled = false;
        if (plugin->feature_flags_manager) {
          enabled = plugin->feature_flags_manager->IsFeatureEnabled(fl_value_get_string(key_value));
        }
        g_autoptr(FlValue) result = fl_value_new_bool(enabled);
        fl_method_call_respond_success(method_call, result, nullptr);
        return;
      }
    }
    // Invalid args - respond with null result
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "getFeatureFlag") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* key_value = fl_value_lookup_string(args, "key");
      if (key_value) {
        std::lock_guard<std::mutex> lock(plugin->config_mutex);
        std::string value;
        if (plugin->feature_flags_manager) {
          value = plugin->feature_flags_manager->GetFeatureFlag(fl_value_get_string(key_value));
        }
        g_autoptr(FlValue) result = value.empty() ? nullptr : fl_value_new_string(value.c_str());
        fl_method_call_respond_success(method_call, result, nullptr);
        return;
      }
    }
    // Invalid args - respond with null result
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "reloadFeatureFlags") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    if (plugin->initialized && !plugin->opt_out && plugin->feature_flags_manager && plugin->storage_manager) {
      std::string distinct_id = get_or_create_distinct_id(plugin->storage_manager);
      std::map<std::string, std::string> properties;
      plugin->feature_flags_manager->ReloadFeatureFlags(distinct_id, properties);
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "getSessionId") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    std::string session_id;
    if (plugin->storage_manager) {
      session_id = get_or_create_session_id(plugin->storage_manager);
    }
    g_autoptr(FlValue) result = session_id.empty() ? nullptr : fl_value_new_string(session_id.c_str());
    fl_method_call_respond_success(method_call, result, nullptr);
  } else if (strcmp(method, "createNewSession") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    
    if (!plugin->initialized || !plugin->storage_manager) {
      fl_method_call_respond_success(method_call, nullptr, nullptr);
      return;
    }
    
    // Generate a new session ID
    std::string session_id = generate_uuid();
    plugin->storage_manager->SetSessionId(session_id);
    
    // Send session initialization event to establish new session context
    std::string distinct_id = get_or_create_distinct_id(plugin->storage_manager);
    posthog::PostHogEvent init_event;
    init_event.event = "$screen";
    init_event.distinct_id = distinct_id;
    init_event.timestamp = get_current_timestamp_ms();
    init_event.properties["$screen_name"] = "Session Started";
    init_event.properties["$session_id"] = session_id;
    init_event.properties["$window_id"] = "main";
    init_event.properties["$lib"] = "posthog-flutter";
    init_event.properties["$lib_version"] = "5.9.0";
    init_event.properties["$device_type"] = "Mobile";
    init_event.properties["$os"] = "Linux";
    
    json init_event_json = init_event.to_json();
    plugin->storage_manager->EnqueueEvent(init_event_json.dump());
    
    PostHogLogger::Debug("New session created with session_id: " + session_id);
    
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "openUrl") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_STRING) {
      std::string url = fl_value_get_string(args);
      std::string command = "xdg-open \"" + url + "\" &";
      int rc = system(command.c_str());
      (void)rc;  // explicitly ignore result
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "alias") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* alias_value = fl_value_lookup_string(args, "alias");
      if (alias_value && fl_value_get_type(alias_value) == FL_VALUE_TYPE_STRING) {
        std::lock_guard<std::mutex> lock(plugin->config_mutex);
        if (plugin->storage_manager) {
          std::string old_id = get_or_create_distinct_id(plugin->storage_manager);
          std::string new_id = fl_value_get_string(alias_value);
          
          // Capture alias event
          // Build alias event using structs
          posthog::PostHogEvent event;
          event.event = "$create_alias";
          event.distinct_id = new_id;
          event.timestamp = get_current_timestamp_ms();
          event.properties = json::object();
          event.properties["alias"] = old_id;
          
          json event_json_obj = event.to_json();
          plugin->storage_manager->EnqueueEvent(event_json_obj.dump());
          plugin->storage_manager->SetDistinctId(new_id);
        }
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "group") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* group_type_value = fl_value_lookup_string(args, "groupType");
      FlValue* group_key_value = fl_value_lookup_string(args, "groupKey");
      if (group_type_value && group_key_value) {
        std::lock_guard<std::mutex> lock(plugin->config_mutex);
        if (plugin->initialized && !plugin->opt_out && plugin->storage_manager) {
          // Build group identify event using structs
          posthog::PostHogEvent event;
          event.event = "$groupidentify";
          event.distinct_id = get_or_create_distinct_id(plugin->storage_manager);
          event.timestamp = get_current_timestamp_ms();
          event.properties["$group_type"] = fl_value_get_string(group_type_value);
          event.properties["$group_key"] = fl_value_get_string(group_key_value);
          
          json event_json_obj = event.to_json();
          plugin->storage_manager->EnqueueEvent(event_json_obj.dump());
        }
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "captureException") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      // properties_value is available but not used in simplified implementation
      (void)fl_value_lookup_string(args, "properties");
      std::lock_guard<std::mutex> lock(plugin->config_mutex);
      if (plugin->initialized && !plugin->opt_out && plugin->storage_manager) {
        // Build exception event using structs
        posthog::PostHogEvent event;
        event.event = "$exception";
        event.distinct_id = get_or_create_distinct_id(plugin->storage_manager);
        event.timestamp = get_current_timestamp_ms();
        event.properties = json::object();
        
        json event_json_obj = event.to_json();
        plugin->storage_manager->EnqueueEvent(event_json_obj.dump());
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "getFeatureFlagPayload") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP) {
      FlValue* key_value = fl_value_lookup_string(args, "key");
      if (key_value) {
        std::lock_guard<std::mutex> lock(plugin->config_mutex);
        std::string payload;
        if (plugin->feature_flags_manager) {
          payload = plugin->feature_flags_manager->GetFeatureFlagPayload(fl_value_get_string(key_value));
        }
        g_autoptr(FlValue) result = payload.empty() ? nullptr : fl_value_new_string(payload.c_str());
        fl_method_call_respond_success(method_call, result, nullptr);
        return;
      }
    }
    // Invalid args - respond with null result
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "close") == 0) {
    {
      std::lock_guard<std::mutex> lock(plugin->config_mutex);
      plugin->should_flush = false;
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
    // Join thread after releasing lock
    if (plugin->flush_thread.joinable()) {
      plugin->flush_thread.join();
    }
  } else if (strcmp(method, "sendFullSnapshot") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP && plugin->session_replay_manager) {
      FlValue* image_bytes_value = fl_value_lookup_string(args, "imageBytes");
      FlValue* id_value = fl_value_lookup_string(args, "id");
      FlValue* x_value = fl_value_lookup_string(args, "x");
      FlValue* y_value = fl_value_lookup_string(args, "y");
      FlValue* width_value = fl_value_lookup_string(args, "width");
      FlValue* height_value = fl_value_lookup_string(args, "height");
      
      if (image_bytes_value && id_value && x_value && y_value && width_value && height_value &&
          fl_value_get_type(image_bytes_value) == FL_VALUE_TYPE_UINT8_LIST &&
          fl_value_get_type(id_value) == FL_VALUE_TYPE_INT &&
          fl_value_get_type(x_value) == FL_VALUE_TYPE_INT &&
          fl_value_get_type(y_value) == FL_VALUE_TYPE_INT &&
          fl_value_get_type(width_value) == FL_VALUE_TYPE_INT &&
          fl_value_get_type(height_value) == FL_VALUE_TYPE_INT) {
        
        const uint8_t* data = fl_value_get_uint8_list(image_bytes_value);
        size_t data_length = fl_value_get_length(image_bytes_value);
        
        std::vector<uint8_t> image_data(data, data + data_length);
        PostHogLogger::Debug("[Replay] Received snapshot: id=" + std::to_string(fl_value_get_int(id_value))
                    + ", size=" + std::to_string(data_length) + " bytes, dimensions=" 
                    + std::to_string(fl_value_get_int(width_value)) + "x" + std::to_string(fl_value_get_int(height_value)));
        
        plugin->session_replay_manager->AddSnapshot(
          image_data,
          fl_value_get_int(id_value),
          fl_value_get_int(x_value),
          fl_value_get_int(y_value),
          fl_value_get_int(width_value),
          fl_value_get_int(height_value)
        );
        
        fl_method_call_respond_success(method_call, nullptr, nullptr);
        return;
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "sendMetaEvent") == 0) {
    if (args && fl_value_get_type(args) == FL_VALUE_TYPE_MAP && plugin->session_replay_manager) {
      FlValue* width_value = fl_value_lookup_string(args, "width");
      FlValue* height_value = fl_value_lookup_string(args, "height");
      FlValue* screen_value = fl_value_lookup_string(args, "screen");
      
      if (width_value && height_value &&
          fl_value_get_type(width_value) == FL_VALUE_TYPE_INT &&
          fl_value_get_type(height_value) == FL_VALUE_TYPE_INT) {
        
        std::string screen = "";
        if (screen_value && fl_value_get_type(screen_value) == FL_VALUE_TYPE_STRING) {
          screen = fl_value_get_string(screen_value);
        }
        
        PostHogLogger::Debug("[Replay] Received meta event: dimensions=" 
                    + std::to_string(fl_value_get_int(width_value)) + "x" + std::to_string(fl_value_get_int(height_value))
                    + ", screen=" + screen);
        
        plugin->session_replay_manager->AddMetaEvent(
          fl_value_get_int(width_value),
          fl_value_get_int(height_value),
          screen
        );
        
        fl_method_call_respond_success(method_call, nullptr, nullptr);
        return;
      }
    }
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else if (strcmp(method, "isSessionReplayActive") == 0) {
    std::lock_guard<std::mutex> lock(plugin->config_mutex);
    bool active = false;
    if (plugin->session_replay_manager) {
      active = plugin->session_replay_manager->IsActive();
    }
    g_autoptr(FlValue) result = fl_value_new_bool(active);
    fl_method_call_respond_success(method_call, result, nullptr);
  } else {
    fl_method_call_respond_not_implemented(method_call, nullptr);
  }
}

void posthog_flutter_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  PosthogFlutterPlugin* plugin = POSTHOG_FLUTTER_PLUGIN(
      g_object_new(posthog_flutter_plugin_get_type(), nullptr));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  plugin->channel =
      fl_method_channel_new(fl_plugin_registrar_get_messenger(registrar),
                            "posthog_flutter",
                            FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(plugin->channel, handle_method_call,
                                            g_object_ref(plugin), g_object_unref);

  g_object_unref(plugin);
}
