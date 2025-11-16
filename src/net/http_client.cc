#include "xtils/net/http_client.h"

#include <sstream>
#include <thread>

#include "xtils/logging/logger.h"
#include "xtils/net/http_common.h"

namespace xtils {

// HttpRequest implementation

void HttpRequest::AddHeader(const std::string& name, const std::string& value) {
  headers.emplace_back(name, value);
}

void HttpRequest::SetContentType(const std::string& content_type) {
  AddHeader("Content-Type", content_type);
}

void HttpRequest::SetUserAgent(const std::string& user_agent) {
  AddHeader("User-Agent", user_agent);
}

void HttpRequest::SetAuthorization(const std::string& auth) {
  AddHeader("Authorization", auth);
}

void HttpRequest::SetBody(const std::string& data,
                          const std::string& content_type) {
  body = data;
  if (!content_type.empty()) {
    SetContentType(content_type);
  }
}

void HttpRequest::SetJsonBody(const std::string& json) {
  SetBody(json, "application/json");
}

void HttpRequest::SetFormBody(
    const std::map<std::string, std::string>& form_data) {
  SetBody(HttpUtils::FormDataEncode(form_data),
          "application/x-www-form-urlencoded");
}

// HttpResponse implementation

std::string HttpResponse::GetHeader(const std::string& name) const {
  return HttpUtils::GetHeaderValue(headers, name);
}

bool HttpResponse::HasHeader(const std::string& name) const {
  return HttpUtils::HasHeader(headers, name);
}

// HttpClient implementation

HttpClient::HttpClient(TaskRunner* task_runner,
                       HttpClientEventListener* listener)
    : task_runner_(task_runner),
      listener_(listener),
      tcp_client_(std::make_unique<TcpClient>(task_runner, this)),
      state_(State::kIdle),
      headers_received_(false),
      content_length_(0),
      bytes_received_(0),
      chunked_encoding_(false),
      default_timeout_ms_(30000),
      follow_redirects_(true),
      max_redirects_(5),
      redirect_count_(0),
      keep_alive_(false),
      verify_ssl_(true),
      connected_port_(0),
      connection_reusable_(false) {
  SetUserAgent("xtils-http-client/1.0");
}

HttpClient::~HttpClient() { Cancel(); }

HttpResponse HttpClient::Request(const HttpRequest& request) {
  if (!RequestAsync(request)) {
    HttpResponse error_response;
    error_response.status_code = 0;
    return error_response;
  }

  // Wait for completion (simplified - in real implementation you'd use proper
  // synchronization)
  while (IsBusy()) {
    // This is a simplified sync implementation
    // In practice, you'd want to use proper event loop integration
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return last_response_;
}

bool HttpClient::RequestAsync(const HttpRequest& request) {
  if (IsBusy()) {
    return false;
  }

  current_request_ = request;
  redirect_count_ = 0;

  if (!request.url.IsValid()) {
    HandleError("Invalid URL: " + request.url.ToString());
    return false;
  }

  return SendHttpRequest(request.url);
}

HttpResponse HttpClient::Get(const std::string& url) {
  HttpRequest request;
  request.method = HttpMethod::kGet;
  request.url = HttpUrl(url);
  return Request(request);
}

HttpResponse HttpClient::Post(const std::string& url, const std::string& body,
                              const std::string& content_type) {
  HttpRequest request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.SetBody(body, content_type);
  return Request(request);
}

HttpResponse HttpClient::PostJson(const std::string& url,
                                  const std::string& json) {
  return Post(url, json, "application/json");
}

HttpResponse HttpClient::PostForm(
    const std::string& url,
    const std::map<std::string, std::string>& form_data) {
  return Post(url, HttpUtils::FormDataEncode(form_data),
              "application/x-www-form-urlencoded");
}

bool HttpClient::GetAsync(const std::string& url) {
  HttpRequest request;
  request.method = HttpMethod::kGet;
  request.url = HttpUrl(url);
  return RequestAsync(request);
}

bool HttpClient::PostAsync(const std::string& url, const std::string& body,
                           const std::string& content_type) {
  HttpRequest request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.SetBody(body, content_type);
  return RequestAsync(request);
}

bool HttpClient::PostJsonAsync(const std::string& url,
                               const std::string& json) {
  return PostAsync(url, json, "application/json");
}

void HttpClient::Cancel() {
  if (tcp_client_) {
    tcp_client_->Disconnect();
  }
  state_ = State::kIdle;
  receive_buffer_.clear();
  headers_received_ = false;
}

void HttpClient::SetDefaultHeaders(const HttpHeaders& headers) {
  default_headers_ = headers;
}

void HttpClient::AddDefaultHeader(const std::string& name,
                                  const std::string& value) {
  default_headers_.emplace_back(name, value);
}

void HttpClient::SetUserAgent(const std::string& user_agent) {
  AddDefaultHeader("User-Agent", user_agent);
}

void HttpClient::SetTimeout(uint32_t timeout_ms) {
  default_timeout_ms_ = timeout_ms;
}

void HttpClient::SetFollowRedirects(bool follow, int max_redirects) {
  follow_redirects_ = follow;
  max_redirects_ = max_redirects;
}

void HttpClient::SetKeepAlive(bool keep_alive) { keep_alive_ = keep_alive; }

void HttpClient::SetCookie(const std::string& name, const std::string& value,
                           const std::string& domain) {
  std::string cookie_domain = domain.empty() ? connected_host_ : domain;
  cookies_[cookie_domain][name] = value;
}

void HttpClient::ClearCookies() { cookies_.clear(); }

std::string HttpClient::GetCookies(const std::string& domain) const {
  std::string cookie_domain = domain.empty() ? connected_host_ : domain;
  auto it = cookies_.find(cookie_domain);
  if (it == cookies_.end() || it->second.empty()) {
    return "";
  }

  std::stringstream ss;
  bool first = true;

  for (const auto& cookie : it->second) {
    if (!first) {
      ss << "; ";
    }
    ss << cookie.first << "=" << cookie.second;
    first = false;
  }

  return ss.str();
}

void HttpClient::SetVerifySSL(bool verify) { verify_ssl_ = verify; }

void HttpClient::SetSSLCertificate(const std::string& cert_path) {
  // Placeholder for SSL certificate setting
}

// TcpClientEventListener implementation

void HttpClient::OnConnected(TcpClient* client, bool success) {
  if (!success) {
    HandleError("Failed to connect to server");
    return;
  }

  state_ = State::kSendingRequest;
  std::string http_request = BuildHttpRequest(current_request_);

  if (!client->SendString(http_request)) {
    HandleError("Failed to send HTTP request");
    return;
  }

  state_ = State::kReceivingResponse;
  headers_received_ = false;
  content_length_ = 0;
  bytes_received_ = 0;
  chunked_encoding_ = false;
  receive_buffer_.clear();
}

void HttpClient::OnDataReceived(TcpClient* client, const void* data,
                                size_t len) {
  receive_buffer_.append(static_cast<const char*>(data), len);
  ProcessReceivedData(data, len);
}

void HttpClient::OnDisconnected(TcpClient* client) {
  if (state_ == State::kReceivingResponse) {
    // Connection closed, complete the response if we have enough data
    CompleteRequest();
  }
  connection_reusable_ = false;
}

void HttpClient::OnError(TcpClient* client, const std::string& error) {
  HandleError("TCP error: " + error);
}

// HTTP protocol handling

std::string HttpClient::BuildHttpRequest(const HttpRequest& request) {
  HttpUrl url(request.url);

  std::stringstream ss;
  ss << HttpUtils::HttpMethodToString(request.method) << " " << url.path;

  if (!url.query.empty()) {
    ss << "?" << url.query;
  }

  ss << " HTTP/1.1\r\n";
  ss << "Host: " << url.host;

  if (url.port != url.GetDefaultPort()) {
    ss << ":" << url.port;
  }
  ss << "\r\n";

  // Add headers
  HttpHeaders merged_headers = MergeHeaders(request.headers);
  for (const auto& header : merged_headers) {
    ss << header.name << ": " << header.value << "\r\n";
  }

  // Add cookies
  std::string cookies = BuildCookieHeader(url.host);
  if (!cookies.empty()) {
    ss << "Cookie: " << cookies << "\r\n";
  }

  // Add content length if there's a body
  if (!request.body.empty()) {
    ss << "Content-Length: " << request.body.length() << "\r\n";
  }

  // Connection header
  if (keep_alive_) {
    ss << "Connection: keep-alive\r\n";
  } else {
    ss << "Connection: close\r\n";
  }

  ss << "\r\n";

  // Add body
  if (!request.body.empty()) {
    ss << request.body;
  }

  return ss.str();
}

bool HttpClient::SendHttpRequest(const HttpUrl& url) {
  // Check if we can reuse existing connection
  bool can_reuse = connection_reusable_ && connected_host_ == url.host &&
                   connected_port_ == url.port && tcp_client_->IsConnected();

  if (!can_reuse) {
    if (tcp_client_->IsConnected()) {
      tcp_client_->Disconnect();
    }
    if (!ConnectToHost(url)) {
      return false;
    }
  } else {
    // Reuse existing connection
    OnConnected(tcp_client_.get(), true);
  }

  return true;
}

void HttpClient::ProcessReceivedData(const void* data, size_t len) {
  if (!headers_received_) {
    // Look for end of headers
    size_t header_end = receive_buffer_.find("\r\n\r\n");
    if (header_end != std::string::npos) {
      headers_received_ = true;

      if (!ParseHttpResponse()) {
        HandleError("Failed to parse HTTP response");
        return;
      }

      // Process any body data that was received with headers
      size_t body_start = header_end + 4;
      if (body_start < receive_buffer_.length()) {
        size_t body_len = receive_buffer_.length() - body_start;
        bytes_received_ += body_len;
      }
    }
  } else {
    // Accumulating body data
    bytes_received_ += len;
  }

  // Check if we have received the complete response
  if (headers_received_) {
    bool complete = false;

    if (chunked_encoding_) {
      // Handle chunked encoding (simplified)
      complete = receive_buffer_.find("0\r\n\r\n") != std::string::npos;
    } else if (content_length_ > 0) {
      // Check content length
      size_t header_end = receive_buffer_.find("\r\n\r\n");
      size_t body_size = receive_buffer_.length() - (header_end + 4);
      complete = body_size >= content_length_;
    } else {
      // No content length, connection close indicates end
      complete = false;  // Will be completed in OnDisconnected
    }

    if (complete) {
      CompleteRequest();
    }
  }
}

bool HttpClient::ParseHttpResponse() {
  size_t header_end = receive_buffer_.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return false;
  }

  std::string header_text = receive_buffer_.substr(0, header_end);

  // Parse status line
  size_t first_line_end = header_text.find("\r\n");
  if (first_line_end == std::string::npos) {
    return false;
  }

  std::string status_line = header_text.substr(0, first_line_end);
  std::istringstream status_stream(status_line);
  current_response_ = HttpResponse();
  std::string http_version;
  status_stream >> http_version >> current_response_.status_code;

  // Read status message
  std::string word;
  current_response_.status_message.clear();
  while (status_stream >> word) {
    if (!current_response_.status_message.empty()) {
      current_response_.status_message += " ";
    }
    current_response_.status_message += word;
  }

  // Parse headers
  ParseResponseHeaders(header_text.substr(first_line_end + 2));

  // Extract body
  size_t body_start = header_end + 4;
  if (body_start < receive_buffer_.length()) {
    current_response_.body = receive_buffer_.substr(body_start);
  }

  // Check for content length
  std::string content_len_str = current_response_.GetHeader("Content-Length");
  if (!content_len_str.empty()) {
    content_length_ = std::stoul(content_len_str);
    current_response_.content_length = content_length_;
  }

  // Check for chunked encoding
  std::string transfer_encoding =
      current_response_.GetHeader("Transfer-Encoding");
  chunked_encoding_ = (transfer_encoding.find("chunked") != std::string::npos);
  current_response_.chunked_encoding = chunked_encoding_;

  return true;
}

void HttpClient::HandleRedirect() {
  redirect_count_++;
  if (!follow_redirects_ || redirect_count_ >= max_redirects_) {
    HandleError("redirect count exceeded limit");
    return;
  }

  std::string location = current_response_.GetHeader("Location");
  if (!location.empty()) {
    HttpUrl new_url(current_request_.url);
    if (location[0] == '/') {
      new_url.path = location;
    } else {
      new_url = HttpUrl(location);
    }
    LogI("Redirecting to: %s, %s", new_url.ToString().c_str(),
         location.c_str());
    current_request_.url = new_url;
    if (!new_url.IsValid()) {
      HandleError("invalid URL");
      return;
    }

    if (listener_) {
      listener_->OnRedirect(this, location);
    }
    current_request_.url = new_url;
    // Send new request
    if (!SendHttpRequest(new_url)) {
      HandleError("failed to send request");
    }
  } else {
    CompleteRequest();
  }
}

void HttpClient::CompleteRequest() {
  last_response_ = current_response_;

  // Handle redirects
  if (current_response_.IsRedirect() && follow_redirects_) {
    HandleRedirect();
    return;
  }

  // Process Set-Cookie headers
  for (const auto& header : current_response_.headers) {
    if (header.name == "Set-Cookie") {
      ProcessSetCookieHeader(header.value, connected_host_);
    }
  }

  // Check if connection can be reused
  std::string connection = current_response_.GetHeader("Connection");
  connection_reusable_ =
      keep_alive_ && (connection != "close") && tcp_client_->IsConnected();

  if (listener_) {
    listener_->OnHttpResponse(this, current_response_);
  }

  state_ = State::kCompleted;
}

void HttpClient::HandleError(const std::string& error) {
  LogI("%s", error.c_str());
  state_ = State::kError;
  connection_reusable_ = false;

  if (listener_) {
    listener_->OnHttpError(this, error);
  }

  state_ = State::kIdle;
}

bool HttpClient::ConnectToHost(const HttpUrl& url) {
  state_ = State::kConnecting;
  connected_host_ = url.host;
  connected_port_ = url.port;

  return tcp_client_->ConnectToHost(url.host, url.port);
}

HttpHeaders HttpClient::MergeHeaders(const HttpHeaders& request_headers) {
  HttpHeaders merged = default_headers_;

  // Add request-specific headers
  for (const auto& header : request_headers) {
    merged.push_back(header);
  }

  return merged;
}

std::string HttpClient::FormatHeaders(const HttpHeaders& headers) {
  std::stringstream ss;
  for (const auto& header : headers) {
    ss << header.name << ": " << header.value << "\r\n";
  }
  return ss.str();
}

void HttpClient::ParseResponseHeaders(const std::string& header_text) {
  std::istringstream stream(header_text);
  std::string line;

  current_response_.headers.clear();

  while (std::getline(stream, line)) {
    if (line.empty() || line == "\r") {
      break;
    }

    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    size_t colon_pos = line.find(':');
    if (colon_pos != std::string::npos) {
      std::string name = line.substr(0, colon_pos);
      std::string value = line.substr(colon_pos + 1);

      // Trim whitespace
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);

      current_response_.headers.emplace_back(name, value);
    }
  }
}

void HttpClient::ProcessSetCookieHeader(const std::string& cookie_header,
                                        const std::string& domain) {
  // Simple cookie parsing (production code would be more robust)
  size_t eq_pos = cookie_header.find('=');
  if (eq_pos != std::string::npos) {
    std::string name = cookie_header.substr(0, eq_pos);

    size_t semicolon = cookie_header.find(';', eq_pos);
    std::string value = cookie_header.substr(
        eq_pos + 1, semicolon == std::string::npos ? std::string::npos
                                                   : semicolon - eq_pos - 1);

    cookies_[domain][name] = value;
  }
}

std::string HttpClient::BuildCookieHeader(const std::string& domain) {
  auto it = cookies_.find(domain);
  if (it == cookies_.end() || it->second.empty()) {
    return "";
  }

  std::stringstream ss;
  bool first = true;

  for (const auto& cookie : it->second) {
    if (!first) {
      ss << "; ";
    }
    ss << cookie.first << "=" << cookie.second;
    first = false;
  }

  return ss.str();
}

}  // namespace xtils
