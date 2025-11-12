#include "storage_manager.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

StorageManager::StorageManager() : db_(nullptr) {}

StorageManager::~StorageManager() {
  Close();
}

bool StorageManager::Initialize(const std::string& app_data_dir) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Create directory if it doesn't exist
  struct stat info;
  if (stat(app_data_dir.c_str(), &info) != 0) {
    // Create directory recursively
    std::string cmd = "mkdir -p " + app_data_dir;
    if (system(cmd.c_str()) != 0) {
      return false;
    }
  }

  db_path_ = app_data_dir + "/posthog.db";

  int rc = sqlite3_open(db_path_.c_str(), &db_);
  if (rc != SQLITE_OK) {
    return false;
  }

  return CreateTables();
}

void StorageManager::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool StorageManager::CreateTables() {
  const char* sql_events = R"(
    CREATE TABLE IF NOT EXISTS events (
      id TEXT PRIMARY KEY,
      event_json TEXT NOT NULL,
      created_at INTEGER NOT NULL
    );
  )";

  const char* sql_settings = R"(
    CREATE TABLE IF NOT EXISTS settings (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );
  )";

  const char* sql_super_properties = R"(
    CREATE TABLE IF NOT EXISTS super_properties (
      key TEXT PRIMARY KEY,
      value_json TEXT NOT NULL
    );
  )";

  const char* sql_user_properties = R"(
    CREATE TABLE IF NOT EXISTS user_properties (
      key TEXT PRIMARY KEY,
      value_json TEXT NOT NULL
    );
  )";

  return ExecuteSQL(sql_events) &&
         ExecuteSQL(sql_settings) &&
         ExecuteSQL(sql_super_properties) &&
         ExecuteSQL(sql_user_properties);
}

bool StorageManager::ExecuteSQL(const std::string& sql) {
  if (!db_) return false;

  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    if (err_msg) {
      sqlite3_free(err_msg);
    }
    return false;
  }
  return true;
}

std::string StorageManager::GenerateUUID() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  std::ostringstream oss;
  oss << std::hex;
  for (int i = 0; i < 32; i++) {
    if (i == 8 || i == 12 || i == 16 || i == 20) {
      oss << "-";
    }
    oss << dis(gen);
  }
  return oss.str();
}

bool StorageManager::EnqueueEvent(const std::string& event_json) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  std::string id = GenerateUUID();
  std::string sql = "INSERT INTO events (id, event_json, created_at) VALUES (?, ?, ?)";
  
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, event_json.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 3, time(nullptr));

  bool result = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return result;
}

std::vector<std::string> StorageManager::GetQueuedEvents(int max_count) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> events;

  if (!db_) return events;

  std::string sql = "SELECT id, event_json FROM events ORDER BY created_at ASC LIMIT ?";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return events;
  }

  sqlite3_bind_int(stmt, 1, max_count);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* event_json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    
    std::string event_with_id = std::string(id) + "|" + std::string(event_json);
    events.push_back(event_with_id);
  }

  sqlite3_finalize(stmt);
  return events;
}

bool StorageManager::RemoveEvents(const std::vector<std::string>& event_ids) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_ || event_ids.empty()) return false;

  std::string placeholders;
  for (size_t i = 0; i < event_ids.size(); i++) {
    if (i > 0) placeholders += ",";
    placeholders += "?";
  }

  std::string sql = "DELETE FROM events WHERE id IN (" + placeholders + ")";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  for (size_t i = 0; i < event_ids.size(); i++) {
    sqlite3_bind_text(stmt, i + 1, event_ids[i].c_str(), -1, SQLITE_STATIC);
  }

  bool result = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return result;
}

int StorageManager::GetQueueSize() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return 0;

  std::string sql = "SELECT COUNT(*) FROM events";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }

  sqlite3_finalize(stmt);
  return count;
}

bool StorageManager::SetDistinctId(const std::string& distinct_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  std::string sql = "INSERT OR REPLACE INTO settings (key, value) VALUES ('distinct_id', ?)";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, distinct_id.c_str(), -1, SQLITE_STATIC);
  bool result = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return result;
}

std::string StorageManager::GetDistinctId() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return "";

  std::string sql = "SELECT value FROM settings WHERE key = 'distinct_id'";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return "";
  }

  std::string distinct_id;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    distinct_id = value ? value : "";
  }

  sqlite3_finalize(stmt);
  return distinct_id;
}

bool StorageManager::SetSuperProperty(const std::string& key, const std::string& value_json) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  std::string sql = "INSERT OR REPLACE INTO super_properties (key, value_json) VALUES (?, ?)";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, value_json.c_str(), -1, SQLITE_STATIC);
  bool result = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return result;
}

bool StorageManager::RemoveSuperProperty(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  std::string sql = "DELETE FROM super_properties WHERE key = ?";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
  bool result = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return result;
}

std::map<std::string, std::string> StorageManager::GetAllSuperProperties() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::map<std::string, std::string> properties;

  if (!db_) return properties;

  std::string sql = "SELECT key, value_json FROM super_properties";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return properties;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (key && value) {
      properties[key] = value;
    }
  }

  sqlite3_finalize(stmt);
  return properties;
}

bool StorageManager::SetFeatureFlags(const std::string& flags_json) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  std::string sql = "INSERT OR REPLACE INTO settings (key, value) VALUES ('feature_flags', ?)";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, flags_json.c_str(), -1, SQLITE_STATIC);
  bool result = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return result;
}

std::string StorageManager::GetFeatureFlags() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return "{}";

  std::string sql = "SELECT value FROM settings WHERE key = 'feature_flags'";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return "{}";
  }

  std::string flags = "{}";
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    flags = value ? value : "{}";
  }

  sqlite3_finalize(stmt);
  return flags;
}

bool StorageManager::SetOptOut(bool opt_out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  std::string sql = "INSERT OR REPLACE INTO settings (key, value) VALUES ('opt_out', ?)";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  std::string value = opt_out ? "1" : "0";
  sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_STATIC);
  bool result = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return result;
}

bool StorageManager::GetOptOut() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  std::string sql = "SELECT value FROM settings WHERE key = 'opt_out'";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  bool opt_out = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    opt_out = (value && std::string(value) == "1");
  }

  sqlite3_finalize(stmt);
  return opt_out;
}

bool StorageManager::SetSessionId(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  std::string sql = "INSERT OR REPLACE INTO settings (key, value) VALUES ('session_id', ?)";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
  bool result = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return result;
}

std::string StorageManager::GetSessionId() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return "";

  std::string sql = "SELECT value FROM settings WHERE key = 'session_id'";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return "";
  }

  std::string session_id;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    session_id = value ? value : "";
  }

  sqlite3_finalize(stmt);
  return session_id;
}

bool StorageManager::SetUserProperties(const std::string& properties_json) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  // Clear existing properties
  ExecuteSQL("DELETE FROM user_properties");

  // Parse and insert new properties (simplified - assumes JSON object)
  // For a full implementation, you'd want to parse the JSON properly
  std::string sql = "INSERT INTO user_properties (key, value_json) VALUES (?, ?)";
  // This is a simplified version - full implementation would parse JSON
  return true;
}

std::string StorageManager::GetUserProperties() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return "{}";

  // Build JSON object from user_properties table
  // Simplified - full implementation would build proper JSON
  return "{}";
}

