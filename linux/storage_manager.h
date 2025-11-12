#ifndef STORAGE_MANAGER_H_
#define STORAGE_MANAGER_H_

#include <glib.h>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>

class StorageManager {
 public:
  StorageManager();
  ~StorageManager();

  bool Initialize(const std::string& app_data_dir);
  void Close();

  // Event queue management
  bool EnqueueEvent(const std::string& event_json);
  std::vector<std::string> GetQueuedEvents(int max_count);
  bool RemoveEvents(const std::vector<std::string>& event_ids);
  int GetQueueSize();

  // Distinct ID management
  bool SetDistinctId(const std::string& distinct_id);
  std::string GetDistinctId();

  // Super properties
  bool SetSuperProperty(const std::string& key, const std::string& value_json);
  bool RemoveSuperProperty(const std::string& key);
  std::map<std::string, std::string> GetAllSuperProperties();

  // Feature flags cache
  bool SetFeatureFlags(const std::string& flags_json);
  std::string GetFeatureFlags();

  // Opt-out state
  bool SetOptOut(bool opt_out);
  bool GetOptOut();

  // Session ID
  bool SetSessionId(const std::string& session_id);
  std::string GetSessionId();

  // User properties
  bool SetUserProperties(const std::string& properties_json);
  std::string GetUserProperties();

 private:
  sqlite3* db_;
  std::mutex mutex_;
  std::string db_path_;

  bool CreateTables();
  bool ExecuteSQL(const std::string& sql);
  std::string GenerateUUID();
};

#endif  // STORAGE_MANAGER_H_

