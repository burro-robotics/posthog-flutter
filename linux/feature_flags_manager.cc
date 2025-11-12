#include "feature_flags_manager.h"
#include <sstream>
#include <iostream>
#include <algorithm>

// Simple JSON parsing helpers (for a production implementation, use a proper JSON library)
namespace {
  std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    std::string search_key = "\"" + key + "\"";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) return "";
    
    pos = json.find(":", pos);
    if (pos == std::string::npos) return "";
    pos++;
    
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    if (pos >= json.length()) return "";
    
    // Extract value
    if (json[pos] == '"') {
      // String value
      pos++;
      size_t end = json.find('"', pos);
      if (end == std::string::npos) return "";
      return json.substr(pos, end - pos);
    } else if (json[pos] == '{') {
      // Object value - find matching brace
      int depth = 1;
      size_t end = pos + 1;
      while (end < json.length() && depth > 0) {
        if (json[end] == '{') depth++;
        else if (json[end] == '}') depth--;
        end++;
      }
      return json.substr(pos, end - pos);
    } else {
      // Number or boolean
      size_t end = pos;
      while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
        end++;
      }
      std::string value = json.substr(pos, end - pos);
      // Trim whitespace
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);
      return value;
    }
  }
}

FeatureFlagsManager::FeatureFlagsManager(HttpClient* http_client, StorageManager* storage_manager)
    : http_client_(http_client), storage_manager_(storage_manager) {
  LoadCachedFlags();
}

void FeatureFlagsManager::LoadCachedFlags() {
  std::string cached_flags = storage_manager_->GetFeatureFlags();
  if (!cached_flags.empty() && cached_flags != "{}") {
    ParseFlagsResponse(cached_flags);
  }
}

void FeatureFlagsManager::ParseFlagsResponse(const std::string& response_json) {
  flags_cache_.clear();
  
  // Look for "featureFlags" key in response
  std::string flags_json = ExtractJsonValue(response_json, "featureFlags");
  if (flags_json.empty() || flags_json[0] != '{') {
    return;
  }
  
  // Simple parsing of feature flags object
  // This is a simplified parser - for production use a proper JSON library
  size_t pos = 1; // Skip opening {
  while (pos < flags_json.length()) {
    // Find key
    size_t key_start = flags_json.find('"', pos);
    if (key_start == std::string::npos) break;
    key_start++;
    size_t key_end = flags_json.find('"', key_start);
    if (key_end == std::string::npos) break;
    std::string key = flags_json.substr(key_start, key_end - key_start);
    
    // Find value
    pos = flags_json.find(':', key_end);
    if (pos == std::string::npos) break;
    pos++;
    
    // Skip whitespace
    while (pos < flags_json.length() && (flags_json[pos] == ' ' || flags_json[pos] == '\t')) pos++;
    
    if (pos >= flags_json.length()) break;
    
    // Extract value
    std::string value;
    if (flags_json[pos] == '"') {
      // String value
      pos++;
      size_t value_end = flags_json.find('"', pos);
      if (value_end == std::string::npos) break;
      value = flags_json.substr(pos, value_end - pos);
      pos = value_end + 1;
    } else {
      // Boolean or other
      size_t value_end = pos;
      while (value_end < flags_json.length() && 
             flags_json[value_end] != ',' && 
             flags_json[value_end] != '}') {
        value_end++;
      }
      value = flags_json.substr(pos, value_end - pos);
      // Trim whitespace
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);
      pos = value_end;
    }
    
    flags_cache_[key] = value;
    
    // Move to next entry
    pos = flags_json.find(',', pos);
    if (pos == std::string::npos) break;
    pos++;
  }
  
  // Cache the flags
  storage_manager_->SetFeatureFlags(response_json);
}

bool FeatureFlagsManager::ReloadFeatureFlags(const std::string& distinct_id,
                                             const std::map<std::string, std::string>& properties) {
  HttpResponse response = http_client_->PostDecide(distinct_id, properties);
  
  if (response.success && !response.body.empty()) {
    ParseFlagsResponse(response.body);
    return true;
  }
  
  return false;
}

bool FeatureFlagsManager::IsFeatureEnabled(const std::string& flag_key) {
  auto it = flags_cache_.find(flag_key);
  if (it == flags_cache_.end()) {
    return false;
  }
  
  std::string value = it->second;
  // Check if value is true or a non-empty string
  return (value == "true" || value == "1" || (value[0] == '"' && value.length() > 2));
}

std::string FeatureFlagsManager::GetFeatureFlag(const std::string& flag_key) {
  auto it = flags_cache_.find(flag_key);
  if (it == flags_cache_.end()) {
    return "";
  }
  
  std::string value = it->second;
  // Remove quotes if present
  if (value.length() >= 2 && value[0] == '"' && value.back() == '"') {
    return value.substr(1, value.length() - 2);
  }
  return value;
}

std::string FeatureFlagsManager::GetFeatureFlagPayload(const std::string& flag_key) {
  // For now, return empty string - payload support can be added later
  return "";
}

