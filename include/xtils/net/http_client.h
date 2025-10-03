#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "xtils/net/http_common.h"
#include "xtils/net/tcp_client.h"
#include "xtils/tasks/task_runner.h"

namespace xtils {

struct HttpRequest {
  HttpMethod method = HttpMethod::kGet;
  std::string url;
  std::string path = "/";
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

  // Async versions
  bool GetAsync(const std::string& url);
  bool PostAsync(const std::string& url, const std::string& body,
                 const std::string& content_type = "");
  bool PostJsonAsync(const std::string& url, const std::string& json);

  // Cancel current request
  void Cancel();

  // Configuration
  void SetDefaultHeaders(const HttpHeaders& headers);
  void AddDefaultHeader(const std::string& name, const std::string& value);
  void SetUserAgent(const std::string& user_agent);
  void SetTimeout(uint32_t timeout_ms);
  void SetFollowRedirects(bool follow, int max_redirects = 5);
  void SetKeepAlive(bool keep_alive);

  // Cookie management
  void SetCookie(const std::string& name, const std::string& value,
                 const std::string& domain = "");
  void ClearCookies();
  std::string GetCookies(const std::string& domain = "") const;

  // SSL/TLS support (placeholder for future implementation)
  void SetVerifySSL(bool verify);
  void SetSSLCertificate(const std::string& cert_path);

  // State
  State GetState() const { return state_; }
  bool IsBusy() const {
    return state_ != State::kIdle && state_ != State::kCompleted;
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
  void ProcessReceivedData(const void* data, size_t len);
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

  TaskRunner* task_runner_;
  HttpClientEventListener* listener_;
  std::unique_ptr<TcpClient> tcp_client_;
  State state_;

  // Current request/response
  HttpRequest current_request_;
  HttpResponse current_response_;
  HttpResponse last_response_;

  // Response parsing state
  std::string receive_buffer_;
  bool headers_received_;
  size_t content_length_;
  size_t bytes_received_;
  bool chunked_encoding_;

  // Configuration
  HttpHeaders default_headers_;
  uint32_t default_timeout_ms_;
  bool follow_redirects_;
  int max_redirects_;
  int redirect_count_;
  bool keep_alive_;
  bool verify_ssl_;

  // Cookie storage: domain -> cookies
  std::map<std::string, std::map<std::string, std::string>> cookies_;

  // Connection reuse
  std::string connected_host_;
  uint16_t connected_port_;
  bool connection_reusable_;
};

}  // namespace xtils
