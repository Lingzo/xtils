#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "xtils/net/http_common.h"
#include "xtils/net/tcp_client.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

// Multipart form data structures
struct MultipartFile {
  std::string field_name;    // Form field name (e.g., "file")
  std::string filename;      // Filename to send (e.g., "photo.jpg")
  std::string content_type;  // MIME type (e.g., "image/jpeg")
  std::string file_path;     // Path to file on disk
};

struct MultipartField {
  std::string name;
  std::string value;
};

struct HttpRequest {
  HttpMethod method = HttpMethod::kGet;
  HttpUrl url;
  HttpHeaders headers;
  std::string body;
  uint32_t timeout_ms = 30000;  // 30 seconds default

  // Helper methods
  void AddHeader(const std::string& name, const std::string& value);
  void SetContentType(const std::string& content_type);
  void SetUserAgent(const std::string& user_agent);
  void SetAuthorization(const std::string& auth);
  void SetBody(const std::string& data, const std::string& content_type = "");
  void SetJsonBody(const std::string& json);
  void SetFormBody(const std::map<std::string, std::string>& form_data);
  void SetMultipartBody(const std::vector<MultipartField>& fields,
                        const std::vector<MultipartFile>& files);
  bool is_multipart() const {
    return !multipart_fields.empty() || !multipart_files.empty();
  }

  // Multipart is handled differently - don't load into body
  std::vector<MultipartField> multipart_fields;
  std::vector<MultipartFile> multipart_files;
  std::string boundary;  // For multipart/form-data
};

struct HttpResponse {
  int status_code = 0;
  std::string status_message;
  HttpHeaders headers;
  std::string body;
  size_t content_length = 0;
  bool chunked_encoding = false;

  // Helper methods
  std::string GetHeader(const std::string& name) const;
  bool HasHeader(const std::string& name) const;
  bool IsSuccessful() const { return status_code >= 200 && status_code < 300; }
  bool IsRedirect() const { return status_code >= 300 && status_code < 400; }
  bool IsError() const { return status_code >= 400; }
};

class HttpClient;

class HttpClientEventListener {
 public:
  virtual ~HttpClientEventListener() = default;

  // Called when request completes successfully
  virtual void OnHttpResponse(HttpClient* client,
                              const HttpResponse& response) = 0;

  // Called when request fails
  virtual void OnHttpError(HttpClient* client, const std::string& error) = 0;

  // Called for upload/download progress
  virtual void OnProgress(HttpClient* client, size_t bytes_transferred,
                          size_t total_bytes) {}

  // Called when following redirects
  virtual void OnRedirect(HttpClient* client, const std::string& new_url) {}

  // Called when receiving body data (for streaming large files)
  // Return false to stop receiving more data
  virtual bool OnBodyData(HttpClient* client, const void* data, size_t len) {
    return true;
  }
};

class HttpClient : public TcpClientEventListener {
 public:
  enum class State {
    kIdle = 0,
    kConnecting,
    kSendingRequest,
    kReceivingResponse,
    kCompleted,
    kError
  };

  explicit HttpClient(TaskRunner* task_runner,
                      HttpClientEventListener* listener = nullptr);
  ~HttpClient() override;

  // Synchronous HTTP request
  HttpResponse Request(const HttpRequest& request);

  // Asynchronous HTTP request
  bool RequestAsync(const HttpRequest& request);

  // Convenience methods for common HTTP operations
  HttpResponse Get(const std::string& url);
  HttpResponse Post(const std::string& url, const std::string& body,
                    const std::string& content_type = "");
  HttpResponse PostJson(const std::string& url, const std::string& json);
  HttpResponse PostForm(const std::string& url,
                        const std::map<std::string, std::string>& form_data);
  HttpResponse PostMultipart(const std::string& url,
                             const std::vector<MultipartField>& fields,
                             const std::vector<MultipartFile>& files);

  // Async versions
  bool GetAsync(const std::string& url);
  bool PostAsync(const std::string& url, const std::string& body,
                 const std::string& content_type = "");
  bool PostJsonAsync(const std::string& url, const std::string& json);
  bool PostMultipartAsync(const std::string& url,
                          const std::vector<MultipartField>& fields,
                          const std::vector<MultipartFile>& files);

  // Cancel current request
  void Cancel();

  // Configuration
  void SetDefaultHeaders(const HttpHeaders& headers);
  void AddDefaultHeader(const std::string& name, const std::string& value);
  void SetUserAgent(const std::string& user_agent);
  void SetTimeout(uint32_t timeout_ms);
  void SetFollowRedirects(bool follow, int max_redirects = 5);
  void SetKeepAlive(bool keep_alive);
  void SetMaxReceiveBufferSize(size_t max_size);

  // Cookie management
  void SetCookie(const std::string& name, const std::string& value,
                 const std::string& domain = "");
  void ClearCookies();
  std::string GetCookies(const std::string& domain = "") const;

  // SSL/TLS support (placeholder for future implementation)
  void SetVerifySSL(bool verify);
  void SetSSLCertificate(const std::string& cert_path);

  // State
  State GetState() const { return state_.load(); }
  bool IsBusy() const {
    State current_state = state_.load();
    return current_state != State::kIdle &&
           current_state != State::kCompleted && current_state != State::kError;
  }

  // Get last response (for sync requests)
  const HttpResponse& GetLastResponse() const { return last_response_; }

 private:
  // TcpClientEventListener implementation
  void OnConnected(TcpClient* client, bool success) override;
  void OnDataReceived(TcpClient* client, const void* data, size_t len) override;
  void OnDisconnected(TcpClient* client) override;
  void OnError(TcpClient* client, const std::string& error) override;

  // HTTP protocol handling
  std::string BuildHttpRequest(const HttpRequest& request);
  bool SendHttpRequest(const HttpRequest& request);
  bool SendMultipartBody(
      const HttpRequest& request);  // Send multipart body in chunks
  void ProcessReceivedData(const void* data, size_t len);
  void ProcessHeaders();
  void ProcessBody(const void* data, size_t len);
  bool IsResponseComplete();
  bool ParseHttpResponse();
  void HandleRedirect();
  void CompleteRequest();
  void HandleError(const std::string& error);

  // URL parsing and connection
  bool ParseUrl(const std::string& url, HttpUrl& parsed_url);
  bool ConnectToHost(const HttpUrl& url);

  // Header utilities
  HttpHeaders MergeHeaders(const HttpHeaders& request_headers);
  std::string FormatHeaders(const HttpHeaders& headers);
  void ParseResponseHeaders(const std::string& header_text);

  // Cookie utilities
  void ProcessSetCookieHeader(const std::string& cookie_header,
                              const std::string& domain);
  std::string BuildCookieHeader(const std::string& domain);

  // Multipart utilities
  std::string GenerateBoundary();
  size_t CalculateMultipartSize(const std::vector<MultipartField>& fields,
                                const std::vector<MultipartFile>& files,
                                const std::string& boundary);

  void CheckBufferSizeLimit();

  // Request state - encapsulated to avoid forgetting to reset
  struct RequestState {
    HttpRequest request;
    HttpResponse response;
    std::string receive_buffer;
    bool headers_received = false;
    size_t content_length = 0;
    size_t bytes_received = 0;
    bool chunked_encoding = false;
    int redirect_count = 0;
    uint32_t timeout_ms = 0;
    std::atomic<bool> timeout_scheduled{false};
    std::atomic<bool> completed{false};

    size_t bytes_sent = 0;
    size_t total_bytes = 0;

    void Reset() {
      request = HttpRequest();
      response = HttpResponse();
      receive_buffer.clear();
      headers_received = false;
      content_length = 0;
      bytes_received = 0;
      chunked_encoding = false;
      redirect_count = 0;
      timeout_ms = 0;
      timeout_scheduled.store(false);
      completed.store(false);
      bytes_sent = 0;
      total_bytes = 0;
    }
  };

  TaskRunner* task_runner_;
  HttpClientEventListener* listener_;
  std::unique_ptr<TcpClient> tcp_client_;
  std::atomic<State> state_;

  // Current request state
  RequestState current_;
  HttpResponse last_response_;

  // Configuration
  HttpHeaders default_headers_;
  uint32_t default_timeout_ms_;
  bool follow_redirects_;
  int max_redirects_;
  bool keep_alive_;
  bool verify_ssl_;
  size_t max_receive_buffer_size_;
  bool streaming_mode_;

  // Cookie storage: domain -> cookies
  std::map<std::string, std::map<std::string, std::string>> cookies_;

  bool connection_reusable_;

  // Synchronization for sync Request() method
  std::mutex sync_mutex_;
  std::condition_variable sync_cv_;
};

}  // namespace xtils
