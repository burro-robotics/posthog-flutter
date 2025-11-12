#include "http_client.h"
#include "posthog_models.h"
#include "posthog_logger.h"
#include <curl/curl.h>
#include <sstream>
#include <iostream>
#include <cstring>

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  ((std::string*)userp)->append((char*)contents, size * nmemb);
  return size * nmemb;
}

HttpClient::HttpClient() : debug_(false), curl_handle_(nullptr) {}

HttpClient::~HttpClient() {
  if (curl_handle_) {
    curl_easy_cleanup(static_cast<CURL*>(curl_handle_));
    curl_handle_ = nullptr;
  }
  curl_global_cleanup();
}

bool HttpClient::Initialize() {
  // Initialize curl globally (idempotent)
  curl_global_init(CURL_GLOBAL_DEFAULT);
  
  curl_handle_ = curl_easy_init();
  if (!curl_handle_) {
    return false;
  }

  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_WRITEFUNCTION, WriteCallback);

  return true;
}

void HttpClient::SetBaseUrl(const std::string& base_url) {
  base_url_ = base_url;
  // Ensure base_url doesn't end with /
  if (!base_url_.empty() && base_url_.back() == '/') {
    base_url_.pop_back();
  }
}

void HttpClient::SetApiKey(const std::string& api_key) {
  api_key_ = api_key;
}

void HttpClient::SetDebug(bool debug) {
  std::lock_guard<std::mutex> lock(curl_mutex_);
  debug_ = debug;
  // Disable curl verbose output - we use our own logger instead
  // CURLOPT_VERBOSE produces too much noise (connection details, etc.)
  if (curl_handle_) {
    curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_VERBOSE, 0L);
  }
}

HttpResponse HttpClient::PerformPost(const std::string& endpoint, const std::string& body) {
  HttpResponse response;
  response.success = false;
  response.status_code = 0;

  // CRITICAL: Lock mutex to prevent concurrent access to curl handle
  // Multiple threads (flush thread, session replay thread) can call this simultaneously
  std::lock_guard<std::mutex> lock(curl_mutex_);

  if (!curl_handle_ || base_url_.empty()) {
    return response;
  }

  std::string url = base_url_ + endpoint;
  std::string response_body;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  // Reset curl handle state before reuse
  curl_easy_reset(static_cast<CURL*>(curl_handle_));
  
  // Re-setup curl options (needed after reset)
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_WRITEFUNCTION, WriteCallback);
  // Always disable curl verbose - we use our own logger
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_VERBOSE, 0L);

  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_URL, url.c_str());
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(static_cast<CURL*>(curl_handle_), CURLOPT_WRITEDATA, &response_body);

  CURLcode res = curl_easy_perform(static_cast<CURL*>(curl_handle_));

  if (res == CURLE_OK) {
    curl_easy_getinfo(static_cast<CURL*>(curl_handle_), CURLINFO_RESPONSE_CODE, &response.status_code);
    response.body = response_body;
    response.success = (response.status_code >= 200 && response.status_code < 300);
  } else {
    PostHogLogger::Error("HTTP request failed: " + std::string(curl_easy_strerror(res)));
  }

  curl_slist_free_all(headers);
  return response;
}

std::string HttpClient::BuildCapturePayload(const std::vector<std::string>& events) {
  // Parse JSON strings into PostHogEvent structs, then rebuild as proper JSON
  posthog::PostHogBatch batch;
  batch.api_key = api_key_;
  
  for (const auto& event_str : events) {
    try {
      json event_json = json::parse(event_str);
      posthog::PostHogEvent event;
      event.event = event_json["event"].get<std::string>();
      event.distinct_id = event_json["distinct_id"].get<std::string>();
      // Timestamp might be string or number
      if (event_json["timestamp"].is_string()) {
        event.timestamp = std::stoll(event_json["timestamp"].get<std::string>());
      } else {
        event.timestamp = event_json["timestamp"].get<int64_t>();
      }
      event.properties = event_json["properties"];
      batch.batch.push_back(event);
    } catch (const json::exception& e) {
      // Fallback to old method if JSON parsing fails
      std::ostringstream oss;
      oss << "{\"api_key\":\"" << api_key_ << "\",\"batch\":[";
      for (size_t i = 0; i < events.size(); i++) {
        if (i > 0) oss << ",";
        oss << events[i];
      }
      oss << "]}";
      return oss.str();
    }
  }
  
  return batch.to_string();
}

std::string HttpClient::BuildDecidePayload(const std::string& distinct_id,
                                           const std::map<std::string, std::string>& properties) {
  posthog::PostHogDecidePayload payload;
  payload.api_key = api_key_;
  payload.distinct_id = distinct_id;
  
  // Convert map to JSON object
  for (const auto& pair : properties) {
    payload.properties[pair.first] = pair.second;
  }
  
  return payload.to_string();
}

HttpResponse HttpClient::PostCapture(const std::vector<std::string>& events) {
  if (events.empty()) {
    HttpResponse response;
    response.success = false;
    return response;
  }

  std::string payload = BuildCapturePayload(events);
  
  HttpResponse response = PerformPost("/capture/", payload);
  
  // Only log response body if it contains an error (not just "Ok" status)
  if (!response.success && !response.body.empty()) {
    // Check if response body contains error information
    if (response.body.find("error") != std::string::npos || 
        response.body.find("Error") != std::string::npos ||
        response.body.find("failed") != std::string::npos) {
      PostHogLogger::Error("Response body: " + response.body);
    }
  }
  
  return response;
}

HttpResponse HttpClient::PostDecide(const std::string& distinct_id,
                                    const std::map<std::string, std::string>& properties) {
  std::string payload = BuildDecidePayload(distinct_id, properties);
  PostHogLogger::Debug("Fetching feature flags for distinct_id: " + distinct_id);
  return PerformPost("/decide/", payload);
}

HttpResponse HttpClient::PostSessionReplay(const std::string& payload) {
  PostHogLogger::Debug("Sending session replay data");
  return PerformPost("/capture/", payload);
}
