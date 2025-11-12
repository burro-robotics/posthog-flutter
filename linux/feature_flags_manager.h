#ifndef FEATURE_FLAGS_MANAGER_H_
#define FEATURE_FLAGS_MANAGER_H_

#include <string>
#include <map>
#include "http_client.h"
#include "storage_manager.h"

class FeatureFlagsManager {
 public:
  FeatureFlagsManager(HttpClient* http_client, StorageManager* storage_manager);
  
  bool ReloadFeatureFlags(const std::string& distinct_id,
                          const std::map<std::string, std::string>& properties);
  bool IsFeatureEnabled(const std::string& flag_key);
  std::string GetFeatureFlag(const std::string& flag_key);
  std::string GetFeatureFlagPayload(const std::string& flag_key);

 private:
  HttpClient* http_client_;
  StorageManager* storage_manager_;
  std::map<std::string, std::string> flags_cache_;
  
  void ParseFlagsResponse(const std::string& response_json);
  void LoadCachedFlags();
};

#endif  // FEATURE_FLAGS_MANAGER_H_

