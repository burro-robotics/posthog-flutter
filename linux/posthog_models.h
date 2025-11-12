#ifndef POSTHOG_MODELS_H_
#define POSTHOG_MODELS_H_

#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include <cstdint>

using json = nlohmann::json;

namespace posthog {

// PostHog event structure
struct PostHogEvent {
  std::string event;
  std::string distinct_id;
  int64_t timestamp;
  json properties;
  
  json to_json() const {
    json j;
    j["event"] = event;
    j["distinct_id"] = distinct_id;
    j["timestamp"] = std::to_string(timestamp);
    j["properties"] = properties;
    return j;
  }
};

// PostHog batch payload structure
struct PostHogBatch {
  std::string api_key;
  std::vector<PostHogEvent> batch;
  
  json to_json() const {
    json j;
    j["api_key"] = api_key;
    j["batch"] = json::array();
    for (const auto& event : batch) {
      j["batch"].push_back(event.to_json());
    }
    return j;
  }
  
  std::string to_string() const {
    return to_json().dump();
  }
};

// Session replay wireframe structure
struct SessionReplayWireframe {
  int id;
  int x;
  int y;
  int width;
  int height;
  std::string type;
  std::string base64;
  json style;
  
  json to_json() const {
    json j;
    j["id"] = id;
    j["x"] = x;
    j["y"] = y;
    j["width"] = width;
    j["height"] = height;
    j["type"] = type;
    j["base64"] = base64;
    j["style"] = style;
    return j;
  }
};

// Session replay snapshot data structure
struct SessionReplaySnapshotData {
  json initialOffset;
  std::vector<SessionReplayWireframe> wireframes;
  int64_t timestamp;
  
  json to_json() const {
    json j;
    j["initialOffset"] = initialOffset;
    j["wireframes"] = json::array();
    for (const auto& wireframe : wireframes) {
      j["wireframes"].push_back(wireframe.to_json());
    }
    j["timestamp"] = timestamp;
    return j;
  }
};

// Session replay snapshot event structure
struct SessionReplaySnapshotEvent {
  int type;  // 2 for snapshot, 3 for meta
  SessionReplaySnapshotData data;
  int64_t timestamp;
  
  json to_json() const {
    json j;
    j["type"] = type;
    j["data"] = data.to_json();
    j["timestamp"] = timestamp;
    return j;
  }
};

// Session replay event structure
struct SessionReplayEvent {
  std::string event;
  std::string distinct_id;
  json properties;
  int64_t timestamp;
  
  json to_json() const {
    json j;
    // CRITICAL: event name must always be present - PostHog requires it
    j["event"] = event.empty() ? "$snapshot" : event;
    // CRITICAL: distinct_id must always be present and non-empty - PostHog rejects events without it
    j["distinct_id"] = distinct_id.empty() ? "unknown_user" : distinct_id;
    j["properties"] = properties;
    // Use string timestamp to match regular events format
    j["timestamp"] = std::to_string(timestamp);
    return j;
  }
};

// Session replay batch payload structure
struct SessionReplayBatch {
  std::string api_key;
  std::vector<SessionReplayEvent> batch;
  
  json to_json() const {
    json j;
    j["api_key"] = api_key;
    j["batch"] = json::array();
    for (const auto& event : batch) {
      j["batch"].push_back(event.to_json());
    }
    return j;
  }
  
  std::string to_string() const {
    return to_json().dump();
  }
};

// Feature flags decide payload structure
struct PostHogDecidePayload {
  std::string api_key;
  std::string distinct_id;
  json properties;
  
  json to_json() const {
    json j;
    j["api_key"] = api_key;
    j["distinct_id"] = distinct_id;
    if (!properties.empty()) {
      j["properties"] = properties;
    }
    return j;
  }
  
  std::string to_string() const {
    return to_json().dump();
  }
};

}  // namespace posthog

#endif  // POSTHOG_MODELS_H_
