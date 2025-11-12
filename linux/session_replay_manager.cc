#include "session_replay_manager.h"
#include "http_client.h"
#include "storage_manager.h"
#include "posthog_models.h"
#include "posthog_logger.h"
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <vector>
#include <random>
#include <chrono>

using json = nlohmann::json;

#ifdef HAVE_JPEG
#include <png.h>
#include <jpeglib.h>
#include <setjmp.h>
#endif

// Base64 encoding table
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

SessionReplayManager::SessionReplayManager(HttpClient* http_client, StorageManager* storage_manager, const std::string& api_key)
    : http_client_(http_client),
      storage_manager_(storage_manager),
      api_key_(api_key),
      should_flush_(true),
      is_active_(false),
      compression_quality_(75),
      batch_size_(10),
      batch_interval_ms_(5000),
      max_image_dimension_(0),
      debug_(false),
      meta_event_sent_(false) {
  last_batch_time_ = std::chrono::steady_clock::now();
  flush_thread_ = std::thread(&SessionReplayManager::FlushThread, this);
}

SessionReplayManager::~SessionReplayManager() {
  // Stop the background thread first
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    should_flush_ = false;
    is_active_ = false;  // Also stop accepting new snapshots
  }
  
  // Wait for thread to finish
  if (flush_thread_.joinable()) {
    flush_thread_.join();
  }
  
  // CRITICAL: Null out pointers before flushing to prevent use-after-free
  // The pointers may have been deleted by the plugin
  http_client_ = nullptr;
  storage_manager_ = nullptr;
  
  // Don't flush remaining snapshots - the pointers are now invalid
  // Any remaining snapshots will be lost, but that's better than crashing
}

std::string SessionReplayManager::Base64Encode(const std::vector<uint8_t>& data) {
  std::stringstream ss;
  size_t i = 0;
  
  for (i = 0; i < data.size() - 2; i += 3) {
    uint32_t b = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    ss << base64_chars[(b >> 18) & 0x3F];
    ss << base64_chars[(b >> 12) & 0x3F];
    ss << base64_chars[(b >> 6) & 0x3F];
    ss << base64_chars[b & 0x3F];
  }
  
  if (i < data.size()) {
    uint32_t b = data[i] << 16;
    if (i + 1 < data.size()) {
      b |= data[i + 1] << 8;
    }
    ss << base64_chars[(b >> 18) & 0x3F];
    ss << base64_chars[(b >> 12) & 0x3F];
    if (i + 1 < data.size()) {
      ss << base64_chars[(b >> 6) & 0x3F];
    } else {
      ss << '=';
    }
    ss << '=';
  }
  
  return ss.str();
}

std::vector<uint8_t> SessionReplayManager::ResizeImage(
    const std::vector<uint8_t>& image_data, 
    int original_width, 
    int original_height,
    int& new_width, 
    int& new_height) {
  
  if (max_image_dimension_ <= 0 || 
      (original_width <= max_image_dimension_ && original_height <= max_image_dimension_)) {
    new_width = original_width;
    new_height = original_height;
    return image_data;
  }
  
  // Simple nearest-neighbor resize (for simplicity - could use better algorithm)
  double scale = std::min(
      static_cast<double>(max_image_dimension_) / original_width,
      static_cast<double>(max_image_dimension_) / original_height
  );
  
  new_width = static_cast<int>(original_width * scale);
  new_height = static_cast<int>(original_height * scale);
  
  // For now, return original (full resize implementation would go here)
  // This is a placeholder - full implementation would decode PNG, resize, re-encode
  new_width = original_width;
  new_height = original_height;
  return image_data;
}

// Simple PNG header parser to extract dimensions (fallback when libpng not available)
// Only handles non-interlaced PNGs (most common case)
[[maybe_unused]] static bool ParsePngDimensions(const std::vector<uint8_t>& png_data, int& width, int& height) {
  // PNG signature: 89 50 4E 47 0D 0A 1A 0A
  if (png_data.size() < 24) return false;
  if (png_data[0] != 0x89 || png_data[1] != 0x50 || png_data[2] != 0x4E || png_data[3] != 0x47) {
    return false;
  }
  
  // IHDR chunk starts at byte 16
  // Width: bytes 16-19 (big-endian)
  // Height: bytes 20-23 (big-endian)
  width = (png_data[16] << 24) | (png_data[17] << 16) | (png_data[18] << 8) | png_data[19];
  height = (png_data[20] << 24) | (png_data[21] << 16) | (png_data[22] << 8) | png_data[23];
  
  return width > 0 && height > 0 && width < 100000 && height < 100000;
}

#ifdef HAVE_JPEG
// Error handling for JPEG compression
struct jpeg_error_mgr jerr;
static void jpeg_error_exit(j_common_ptr cinfo) {
  (*cinfo->err->output_message)(cinfo);
  longjmp(*(jmp_buf*)cinfo->client_data, 1);
}

// Decode PNG to RGB data
static bool DecodePngToRgb(const std::vector<uint8_t>& png_data, 
                           std::vector<uint8_t>& rgb_data,
                           int& width, int& height) {
  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) return false;
  
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    return false;
  }
  
  // Set up error handling
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return false;
  }
  
  // Create a memory reader
  struct PngReadData {
    const uint8_t* data;
    size_t size;
    size_t offset;
  };
  
  PngReadData read_data = {png_data.data(), png_data.size(), 0};
  
  png_set_read_fn(png_ptr, &read_data, 
                  [](png_structp png_ptr, png_bytep data, png_size_t length) {
                    auto* rd = static_cast<PngReadData*>(png_get_io_ptr(png_ptr));
                    if (rd->offset + length > rd->size) {
                      png_error(png_ptr, "Read beyond end of data");
                      return;
                    }
                    memcpy(data, rd->data + rd->offset, length);
                    rd->offset += length;
                  });
  
  png_read_info(png_ptr, info_ptr);
  
  width = png_get_image_width(png_ptr, info_ptr);
  height = png_get_image_height(png_ptr, info_ptr);
  
  // Convert to RGB if needed
  png_byte color_type = png_get_color_type(png_ptr, info_ptr);
  png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
  
  if (bit_depth == 16) png_set_strip_16(png_ptr);
  if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
  if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
  }
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png_ptr);
  }
  
  png_read_update_info(png_ptr, info_ptr);
  
  // Allocate row pointers
  row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
  int rowbytes = png_get_rowbytes(png_ptr, info_ptr);
  
  rgb_data.resize(width * height * 3);
  
  for (int y = 0; y < height; y++) {
    row_pointers[y] = (png_byte*)malloc(rowbytes);
  }
  
  png_read_image(png_ptr, row_pointers);
  
  // Convert RGBA to RGB
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int src_idx = y * rowbytes + x * 4;
      int dst_idx = (y * width + x) * 3;
      rgb_data[dst_idx] = row_pointers[y][src_idx];     // R
      rgb_data[dst_idx + 1] = row_pointers[y][src_idx + 1]; // G
      rgb_data[dst_idx + 2] = row_pointers[y][src_idx + 2]; // B
    }
    free(row_pointers[y]);
  }
  
  free(row_pointers);
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  
  return true;
}

// Encode RGB data to JPEG
static bool EncodeRgbToJpeg(const std::vector<uint8_t>& rgb_data,
                            int width, int height, int quality,
                            std::vector<uint8_t>& jpeg_data) {
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  JSAMPROW row_pointer[1];
  
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  
  // Set up memory destination
  unsigned char* outbuffer = nullptr;
  unsigned long outsize = 0;
  jpeg_mem_dest(&cinfo, &outbuffer, &outsize);
  
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);
  
  jpeg_start_compress(&cinfo, TRUE);
  
  int row_stride = width * 3;
  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = const_cast<JSAMPROW>(&rgb_data[cinfo.next_scanline * row_stride]);
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }
  
  jpeg_finish_compress(&cinfo);
  
  jpeg_data.assign(outbuffer, outbuffer + outsize);
  
  free(outbuffer);
  jpeg_destroy_compress(&cinfo);
  
  return true;
}
#endif

std::vector<uint8_t> SessionReplayManager::CompressToJpeg(
    const std::vector<uint8_t>& png_data, 
    int width, 
    int height) {
  
#ifdef HAVE_JPEG
  // Try to parse PNG dimensions if not provided
  int actual_width = width;
  int actual_height = height;
  if (actual_width <= 0 || actual_height <= 0) {
    if (!ParsePngDimensions(png_data, actual_width, actual_height)) {
      PostHogLogger::Debug("[Replay] Failed to parse PNG dimensions, using PNG format");
      return png_data;
    }
  }
  
  // Decode PNG to RGB
  std::vector<uint8_t> rgb_data;
  int decoded_width, decoded_height;
  if (!DecodePngToRgb(png_data, rgb_data, decoded_width, decoded_height)) {
    PostHogLogger::Debug("[Replay] Failed to decode PNG, using PNG format");
    return png_data;
  }
  
  // Use decoded dimensions
  actual_width = decoded_width;
  actual_height = decoded_height;
  
  // Encode RGB to JPEG
  std::vector<uint8_t> jpeg_data;
  if (!EncodeRgbToJpeg(rgb_data, actual_width, actual_height, compression_quality_, jpeg_data)) {
    PostHogLogger::Debug("[Replay] Failed to encode JPEG, using PNG format");
    return png_data;
  }
  
  PostHogLogger::Debug("[Replay] Compressed PNG (" + std::to_string(png_data.size()) 
              + " bytes) to JPEG (" + std::to_string(jpeg_data.size()) + " bytes, quality=" 
              + std::to_string(compression_quality_) + ")");
  
  return jpeg_data;
  
#else
  // JPEG compression not available - return PNG as-is
  return png_data;
#endif
}

void SessionReplayManager::AddSnapshot(
    const std::vector<uint8_t>& png_data, 
    int id, 
    int x, 
    int y, 
    int width, 
    int height) {
  
  if (!is_active_) {
    PostHogLogger::Debug("[Replay] Snapshot ignored - session replay not active");
    return;
  }
  
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  
  // Resize if needed
  int final_width = width;
  int final_height = height;
  std::vector<uint8_t> processed_data = ResizeImage(png_data, width, height, final_width, final_height);
  
  // Compress to JPEG (or keep PNG if compression fails/not available)
  std::vector<uint8_t> compressed = CompressToJpeg(processed_data, final_width, final_height);
  
  // Convert to base64
  std::string base64 = Base64Encode(compressed);
  
  SnapshotData snapshot;
  snapshot.image_base64 = base64;
  snapshot.id = id;
  snapshot.x = x;
  snapshot.y = y;
  snapshot.width = final_width;
  snapshot.height = final_height;
  // Get current timestamp in milliseconds since epoch
  snapshot.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  
  snapshot_buffer_.push_back(snapshot);
  
  PostHogLogger::Debug("[Replay] Snapshot added. Buffer size: " + std::to_string(snapshot_buffer_.size()));
}

void SessionReplayManager::AddMetaEvent(int width, int height, const std::string& screen) {
  if (!is_active_) {
    return;
  }
  
  std::lock_guard<std::mutex> lock(buffer_mutex_);
  
  MetaEventData meta;
  meta.width = width;
  meta.height = height;
  meta.screen = screen;
  // Get current timestamp in milliseconds since epoch
  meta.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  
  meta_event_buffer_.push_back(meta);
  meta_event_sent_ = true;
}

void SessionReplayManager::FlushThread() {
  while (should_flush_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check if we should continue (pointers might be invalid)
    if (!should_flush_ || !is_active_) {
      continue;
    }
    
    // CRITICAL: Check pointers are still valid before proceeding
    // They can become null if plugin is disposed
    if (!http_client_) {
      // http_client_ was deleted, stop processing
      break;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_batch_time_).count();
    
    std::vector<SnapshotData> snapshots;
    std::vector<MetaEventData> meta_events;
    bool should_send = false;
    
    {
      std::lock_guard<std::mutex> lock(buffer_mutex_);
      
      if (snapshot_buffer_.size() >= static_cast<size_t>(batch_size_)) {
        should_send = true;
      } else if (elapsed >= batch_interval_ms_ && !snapshot_buffer_.empty()) {
        should_send = true;
      }
      
      if (should_send) {
        snapshots = snapshot_buffer_;
        meta_events = meta_event_buffer_;
        snapshot_buffer_.clear();
        meta_event_buffer_.clear();
        last_batch_time_ = now;
      }
    }
    
    if (should_send) {
      SendBatch(snapshots, meta_events);
    }
  }
}

void SessionReplayManager::Flush() {
  // CRITICAL: Check pointers before flushing to prevent segfaults
  if (!http_client_) {
    PostHogLogger::Error("[Replay] Cannot flush: http_client_ is null");
    return;
  }
  
  std::vector<SnapshotData> snapshots;
  std::vector<MetaEventData> meta_events;
  
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    if (!snapshot_buffer_.empty() || !meta_event_buffer_.empty()) {
      snapshots = snapshot_buffer_;
      meta_events = meta_event_buffer_;
      snapshot_buffer_.clear();
      meta_event_buffer_.clear();
    }
  }
  
  if (!snapshots.empty() || !meta_events.empty()) {
    SendBatch(snapshots, meta_events);
  }
}

void SessionReplayManager::SendBatch(
    const std::vector<SnapshotData>& snapshots,
    const std::vector<MetaEventData>& meta_events) {
  
  if (snapshots.empty() && meta_events.empty()) {
    return;
  }
  
  // CRITICAL: Check pointers before use to prevent segfaults
  // These pointers can become invalid if the plugin is disposed while
  // the background thread is still running
  if (!http_client_) {
    PostHogLogger::Error("[Replay] Error: http_client_ is null, cannot send batch");
    return;
  }
  
  // Get distinct_id from storage
  // CRITICAL: distinct_id must not be empty - PostHog rejects events without it
  std::string distinct_id;
  if (storage_manager_) {
    try {
      distinct_id = storage_manager_->GetDistinctId();
      if (distinct_id.empty()) {
        // Generate a new distinct_id if none exists
        // Format: timestamp-random (similar to other SDKs)
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);
        int random = dis(gen);
        
        std::ostringstream uuid;
        uuid << timestamp << "-" << random;
        distinct_id = uuid.str();
        storage_manager_->SetDistinctId(distinct_id);
      }
    } catch (const std::exception& e) {
      PostHogLogger::Error("[Replay] Error accessing storage_manager_: " + std::string(e.what()));
      // Generate a fallback distinct_id if storage access fails
      auto now = std::chrono::system_clock::now();
      auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()).count();
      std::ostringstream uuid;
      uuid << "replay_" << timestamp;
      distinct_id = uuid.str();
    }
  } else {
    // No storage manager - generate a temporary distinct_id
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::ostringstream uuid;
    uuid << "replay_" << timestamp;
    distinct_id = uuid.str();
  }
  
  // Final safety check - ensure distinct_id is never empty
  if (distinct_id.empty()) {
    distinct_id = "unknown_user";
  }
  
  // CRITICAL: Get session_id - required for PostHog to link snapshots into sessions
  // Session ID is generated fresh on each app startup (not persisted across restarts)
  std::string session_id;
  if (storage_manager_) {
    try {
      session_id = storage_manager_->GetSessionId();
      // If session_id is empty, generate a fallback (shouldn't happen if setup was called)
      if (session_id.empty()) {
        PostHogLogger::Debug("[Replay] No session_id found, generating fallback");
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        std::ostringstream uuid;
        uuid << "session_" << timestamp;
        session_id = uuid.str();
      }
    } catch (const std::exception& e) {
      PostHogLogger::Error("[Replay] Error getting session_id: " + std::string(e.what()));
      // Generate a fallback session_id if storage access fails
      auto now = std::chrono::system_clock::now();
      auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()).count();
      std::ostringstream uuid;
      uuid << "session_" << timestamp;
      session_id = uuid.str();
    }
  } else {
    // No storage manager - generate a temporary session_id
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::ostringstream uuid;
    uuid << "session_" << timestamp;
    session_id = uuid.str();
  }
  
  // Final safety check - ensure session_id is never empty
  if (session_id.empty()) {
    session_id = "unknown_session";
  }
  
  // Build PostHog session replay batch using structs
  posthog::SessionReplayBatch batch;
  batch.api_key = api_key_;
  
  // Add meta events first
  for (const auto& meta : meta_events) {
    posthog::SessionReplayEvent event;
    event.event = "$snapshot";
    event.distinct_id = distinct_id;
    event.timestamp = meta.timestamp;
    
    // Build snapshot data
    posthog::SessionReplaySnapshotData snapshot_data;
    snapshot_data.initialOffset = json{{"top", 0}, {"left", 0}};
    snapshot_data.timestamp = meta.timestamp;
    
    // Meta event data
    json meta_data = json{
      {"href", meta.screen},
      {"width", meta.width},
      {"height", meta.height}
    };
    
    // Add standard PostHog properties for proper platform identification
    event.properties["$snapshot_source"] = "mobile";  // PostHog uses "mobile" for non-web replay
    event.properties["$session_id"] = session_id;  // CRITICAL: Required to link snapshots into sessions
    event.properties["$window_id"] = "main";  // Window identifier (mobile apps typically have one main window)
    event.properties["$lib"] = "posthog-flutter";
    event.properties["$lib_version"] = "5.9.0";
    event.properties["$device_type"] = "Mobile";
    event.properties["$os"] = "Linux";
    // Add screen dimensions from meta event
    event.properties["$screen_width"] = meta.width;
    event.properties["$screen_height"] = meta.height;
    
    // Meta events use type 4 (not 3) - matching iOS implementation
    event.properties["$snapshot_data"] = json::array();
    event.properties["$snapshot_data"].push_back(json{
      {"type", 4},  // Type 4 for meta events (iOS uses this)
      {"data", meta_data},
      {"timestamp", meta.timestamp}
    });
    
    batch.batch.push_back(event);
  }
  
  // Add snapshot events
  for (const auto& snapshot : snapshots) {
    posthog::SessionReplayEvent event;
    event.event = "$snapshot";
    event.distinct_id = distinct_id;
    event.timestamp = snapshot.timestamp;
    
    // Build wireframe
    posthog::SessionReplayWireframe wireframe;
    wireframe.id = snapshot.id;
    wireframe.x = snapshot.x;
    wireframe.y = snapshot.y;
    wireframe.width = snapshot.width;
    wireframe.height = snapshot.height;
    wireframe.type = "screenshot";
    wireframe.base64 = snapshot.image_base64;
    wireframe.style = json::object();
    
    // Build snapshot data
    posthog::SessionReplaySnapshotData snapshot_data;
    snapshot_data.initialOffset = json{{"top", 0}, {"left", 0}};
    snapshot_data.wireframes.push_back(wireframe);
    snapshot_data.timestamp = snapshot.timestamp;
    
    posthog::SessionReplaySnapshotEvent snapshot_event;
    snapshot_event.type = 2;  // Snapshot event type
    snapshot_event.data = snapshot_data;
    snapshot_event.timestamp = snapshot.timestamp;
    
    // Add standard PostHog properties for proper platform identification
    event.properties["$snapshot_source"] = "mobile";  // PostHog uses "mobile" for non-web replay
    event.properties["$session_id"] = session_id;  // CRITICAL: Required to link snapshots into sessions
    event.properties["$window_id"] = "main";  // Window identifier (mobile apps typically have one main window)
    event.properties["$lib"] = "posthog-flutter";
    event.properties["$lib_version"] = "5.9.0";
    event.properties["$device_type"] = "Mobile";
    event.properties["$os"] = "Linux";
    // Add screen dimensions to match other events
    event.properties["$screen_width"] = snapshot.width;
    event.properties["$screen_height"] = snapshot.height;
    
    // Snapshot events use type 2 with wireframes
    event.properties["$snapshot_data"] = json::array();
    event.properties["$snapshot_data"].push_back(snapshot_event.to_json());
    
    batch.batch.push_back(event);
  }
  
  std::string payload = batch.to_string();
  
  PostHogLogger::Debug("[Replay] Sending batch: " + std::to_string(snapshots.size()) 
              + " snapshots, " + std::to_string(meta_events.size()) + " meta events");
  
  // Log payload preview (first and last 40 chars, no API key)
  std::string payload_preview = payload;
  // Remove API key from payload preview
  size_t api_key_pos = payload_preview.find("\"api_key\"");
  if (api_key_pos != std::string::npos) {
    size_t start = payload_preview.find('"', api_key_pos + 9);
    size_t end = payload_preview.find('"', start + 1);
    if (end != std::string::npos) {
      payload_preview.replace(start + 1, end - start - 1, "***");
    }
  }
  // Show first and last 40 chars
  if (payload_preview.length() > 80) {
    payload_preview = payload_preview.substr(0, 40) + "..." + payload_preview.substr(payload_preview.length() - 40);
  }
  PostHogLogger::Debug("[Replay] Payload preview: " + payload_preview);
  
  // Send to PostHog capture endpoint
  // Wrap in try-catch to prevent crashes from HTTP client issues
  try {
    HttpResponse response = http_client_->PostSessionReplay(payload);
    
    // Info level: Log batch send result (production)
    if (response.success) {
      // Only log in debug mode - session replay batches are frequent
      PostHogLogger::Debug("[Replay] Sent batch successfully: " + std::to_string(snapshots.size()) 
                  + " snapshots, " + std::to_string(meta_events.size()) + " meta events");
    } else {
      PostHogLogger::Error("[Replay] Failed to send batch: HTTP " + std::to_string(response.status_code));
    }
    
    PostHogLogger::Debug("[Replay] Batch sent. Success: " + std::string(response.success ? "true" : "false") 
                + ", Status: " + std::to_string(response.status_code));
    // Only log response body if it contains an error (not just "Ok" status)
    if (!response.success && !response.body.empty()) {
      if (response.body.find("error") != std::string::npos || 
          response.body.find("Error") != std::string::npos ||
          response.body.find("failed") != std::string::npos) {
        PostHogLogger::Error("[Replay] Response body: " + response.body);
      }
    }
    // Log payload size for debugging
    PostHogLogger::Debug("[Replay] Payload size: " + std::to_string(payload.size()) + " bytes");
  } catch (const std::exception& e) {
    PostHogLogger::Error("[Replay] Error sending batch: " + std::string(e.what()));
    // Silently fail to prevent crashes
  } catch (...) {
    PostHogLogger::Error("[Replay] Unknown error sending batch");
    // Silently fail to prevent crashes
  }
}
