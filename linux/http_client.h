#ifndef HTTP_CLIENT_H_
#define HTTP_CLIENT_H_

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "posthog_models.h"

struct HttpResponse {
  int status_code = 0;
  std::string body = "";
  bool success = false;
};

class HttpClient {
 public:
  HttpClient();
  ~HttpClient();

  bool Initialize();
  void SetBaseUrl(const std::string& base_url);
  void SetApiKey(const std::string& api_key);
  void SetDebug(bool debug);

  // Send a batch of events to /capture/
  HttpResponse PostCapture(const std::vector<std::string>& events);

  // Fetch feature flags from /decide/
  HttpResponse PostDecide(const std::string& distinct_id, 
                          const std::map<std::string, std::string>& properties);

  // Send session replay data to /capture/
  HttpResponse PostSessionReplay(const std::string& payload);

 private:
  std::string base_url_;
  std::string api_key_;
  bool debug_;
  void* curl_handle_;
  std::mutex curl_mutex_;  // CRITICAL: Protect curl handle from concurrent access

  HttpResponse PerformPost(const std::string& endpoint, const std::string& body);
  std::string BuildCapturePayload(const std::vector<std::string>& events);
  std::string BuildDecidePayload(const std::string& distinct_id,
                                 const std::map<std::string, std::string>& properties);
};

#endif  // HTTP_CLIENT_H_
