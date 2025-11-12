#ifndef SESSION_REPLAY_MANAGER_H_
#define SESSION_REPLAY_MANAGER_H_

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <memory>

struct SnapshotData {
  std::string image_base64;
  int id;
  int x;
  int y;
  int width;
  int height;
  int64_t timestamp;
};

struct MetaEventData {
  int width;
  int height;
  std::string screen;
  int64_t timestamp;
};

class HttpClient;
class StorageManager;

class SessionReplayManager {
 public:
  SessionReplayManager(HttpClient* http_client, StorageManager* storage_manager, const std::string& api_key);
  ~SessionReplayManager();

  // Add a snapshot to the buffer
  void AddSnapshot(const std::vector<uint8_t>& png_data, int id, int x, int y, int width, int height);

  // Add a meta event
  void AddMetaEvent(int width, int height, const std::string& screen);

  // Check if session replay is active
  bool IsActive() const { return is_active_; }

  // Set active state
  void SetActive(bool active) { is_active_ = active; }

  // Configure compression and batching
  void SetCompressionQuality(int quality) { compression_quality_ = quality; }
  void SetBatchSize(int size) { batch_size_ = size; }
  void SetBatchInterval(int interval_ms) { batch_interval_ms_ = interval_ms; }
  void SetMaxImageDimension(int max_dim) { max_image_dimension_ = max_dim; }
  void SetDebug(bool debug) { debug_ = debug; }

  // Force flush any pending snapshots
  void Flush();

 private:
  // Compress PNG to JPEG with configurable quality
  std::vector<uint8_t> CompressToJpeg(const std::vector<uint8_t>& png_data, int width, int height);

  // Resize image if needed
  std::vector<uint8_t> ResizeImage(const std::vector<uint8_t>& image_data, int original_width, int original_height, int& new_width, int& new_height);

  // Convert binary data to base64
  std::string Base64Encode(const std::vector<uint8_t>& data);

  // Background thread to flush batches
  void FlushThread();

  // Send a batch of snapshots
  void SendBatch(const std::vector<SnapshotData>& snapshots, const std::vector<MetaEventData>& meta_events);

  HttpClient* http_client_;
  StorageManager* storage_manager_;
  std::string api_key_;
  
  std::vector<SnapshotData> snapshot_buffer_;
  std::vector<MetaEventData> meta_event_buffer_;
  std::mutex buffer_mutex_;
  
  std::thread flush_thread_;
  bool should_flush_;
  bool is_active_;
  
  int compression_quality_;
  int batch_size_;
  int batch_interval_ms_;
  int max_image_dimension_;
  bool debug_;
  
  std::chrono::steady_clock::time_point last_batch_time_;
  bool meta_event_sent_;
};

#endif  // SESSION_REPLAY_MANAGER_H_
