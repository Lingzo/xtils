#include "xtils/net/http_client.h"

#include <sstream>

#include "xtils/net/http_common.h"
#include "xtils/utils/string_utils.h"

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

void HttpRequest::SetMultipartBody(const std::vector<MultipartField>& fields,
                                   const std::vector<MultipartFile>& files) {
  multipart_fields = fields;
  multipart_files = files;
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
      state_(State::kIdle),
      default_timeout_ms_(30000),
      follow_redirects_(true),
      max_redirects_(5),
      keep_alive_(true),
      verify_ssl_(true),
      max_receive_buffer_size_(100 * 1024 * 1024),
      streaming_mode_(false),
      connection_reusable_(false) {
  tcp_client_ = std::make_unique<TcpClient>(task_runner_, this);
  SetUserAgent("xtils-http-client/1.0");
}

HttpClient::~HttpClient() { Cancel(); }

HttpResponse HttpClient::Request(const HttpRequest& request) {
  std::unique_lock<std::mutex> lock(sync_mutex_);

  current_.Reset();
  current_.completed.store(false);

  if (!RequestAsync(request)) {
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.status_message = "Failed to start request";
    return error_response;
  }

  auto timeout = std::chrono::milliseconds(
      request.timeout_ms > 0 ? request.timeout_ms : default_timeout_ms_);
  if (!sync_cv_.wait_for(lock, timeout,
                         [this]() { return current_.completed.load(); })) {
    Cancel();
    HttpResponse timeout_response;
    timeout_response.status_code = 0;
    timeout_response.status_message = "Request timeout";
    return timeout_response;
  }

  return last_response_;
}

bool HttpClient::RequestAsync(const HttpRequest& request) {
  if (IsBusy()) {
    return false;
  }

  current_.Reset();
  current_.request = request;
  current_.timeout_ms =
      request.timeout_ms > 0 ? request.timeout_ms : default_timeout_ms_;

  if (!request.url.IsValid()) {
    HandleError("Invalid URL");
    return false;
  }

  // Check if this is a multipart request
  if (request.is_multipart()) {
    std::string boundary =
        request.boundary.empty() ? GenerateBoundary() : request.boundary;
    current_.request.boundary = boundary;
    current_.total_bytes = CalculateMultipartSize(
        request.multipart_fields, request.multipart_files, boundary);
  }

  state_.store(State::kConnecting);
  return ConnectToHost(request.url);
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
  HttpRequest request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.SetFormBody(form_data);
  return Request(request);
}

HttpResponse HttpClient::PostMultipart(
    const std::string& url, const std::vector<MultipartField>& fields,
    const std::vector<MultipartFile>& files) {
  HttpRequest request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.multipart_fields = fields;
  request.multipart_files = files;
  request.boundary = GenerateBoundary();
  return Request(request);
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

bool HttpClient::PostMultipartAsync(const std::string& url,
                                    const std::vector<MultipartField>& fields,
                                    const std::vector<MultipartFile>& files) {
  HttpRequest request;
  request.method = HttpMethod::kPost;
  request.url = HttpUrl(url);
  request.multipart_fields = fields;
  request.multipart_files = files;
  request.boundary = GenerateBoundary();
  return RequestAsync(request);
}

void HttpClient::Cancel() {
  if (tcp_client_) {
    tcp_client_->Disconnect();
  }
  state_.store(State::kIdle);
  current_.Reset();
}

void HttpClient::SetDefaultHeaders(const HttpHeaders& headers) {
  default_headers_ = headers;
}

void HttpClient::AddDefaultHeader(const std::string& name,
                                  const std::string& value) {
  HttpUtils::AddHeader(default_headers_, name, value);
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

void HttpClient::SetMaxReceiveBufferSize(size_t max_size) {
  max_receive_buffer_size_ = max_size;
}

void HttpClient::SetCookie(const std::string& name, const std::string& value,
                           const std::string& domain) {
  std::string cookie_domain = domain.empty() ? "" : domain;
  cookies_[cookie_domain][name] = value;
}

void HttpClient::ClearCookies() { cookies_.clear(); }

std::string HttpClient::GetCookies(const std::string& domain) const {
  std::ostringstream oss;
  bool first = true;

  for (const auto& domain_cookies : cookies_) {
    if (domain_cookies.first.empty() || domain_cookies.first == domain) {
      for (const auto& cookie : domain_cookies.second) {
        if (!first) oss << "; ";
        first = false;
        oss << cookie.first << "=" << cookie.second;
      }
    }
  }

  return oss.str();
}

void HttpClient::SetVerifySSL(bool verify) { verify_ssl_ = verify; }

void HttpClient::SetSSLCertificate(const std::string& cert_path) {
  // TODO: Implement SSL certificate loading
  (void)cert_path;
}

// TcpClientEventListener implementation
void HttpClient::OnConnected(TcpClient* client, bool success) {
  (void)client;

  if (!success) {
    HandleError("Failed to connect to host");
    return;
  }

  state_.store(State::kSendingRequest);

  // Schedule timeout
  if (current_.timeout_ms > 0 && !current_.timeout_scheduled.exchange(true)) {
    task_runner_->PostDelayedTask(
        [this]() {
          if (IsBusy() && !current_.completed.load()) {
            HandleError("Request timeout");
          }
        },
        current_.timeout_ms);
  }

  // Send the HTTP request
  if (!SendHttpRequest(current_.request)) {
    HandleError("Failed to send HTTP request");
  }
}

void HttpClient::OnDataReceived(TcpClient* client, const void* data,
                                size_t len) {
  (void)client;
  state_.store(State::kReceivingResponse);
  ProcessReceivedData(data, len);
}

void HttpClient::OnDisconnected(TcpClient* client) {
  if (state_ == State::kReceivingResponse) {
    // Connection closed, complete the response if we have enough data
    CompleteRequest();
  }
}

void HttpClient::OnError(TcpClient* client, const std::string& error) {
  (void)client;
  HandleError(error);
}

// HTTP protocol handling
std::string HttpClient::BuildHttpRequest(const HttpRequest& request) {
  std::ostringstream oss;

  // Request line
  oss << HttpUtils::HttpMethodToString(request.method) << " ";
  oss << (request.url.path.empty() ? "/" : request.url.path);
  if (!request.url.query.empty()) {
    oss << "?" << request.url.query;
  }
  oss << " HTTP/1.1\r\n";

  // Host header
  oss << "Host: " << request.url.host;
  if ((request.url.scheme == "http" && request.url.port != 80 &&
       request.url.port != 0) ||
      (request.url.scheme == "https" && request.url.port != 443 &&
       request.url.port != 0)) {
    oss << ":" << request.url.port;
  }
  oss << "\r\n";

  // Merge and add headers
  HttpHeaders merged_headers = MergeHeaders(request.headers);
  for (const auto& header : merged_headers) {
    oss << header.name << ": " << header.value << "\r\n";
  }

  // Add cookies
  std::string cookies = GetCookies(request.url.host);
  if (!cookies.empty()) {
    oss << "Cookie: " << cookies << "\r\n";
  }

  // Connection header
  oss << "Connection: " << (keep_alive_ ? "keep-alive" : "close") << "\r\n";

  // Handle multipart
  if (request.is_multipart()) {
    oss << "Content-Type: multipart/form-data; boundary=" << request.boundary
        << "\r\n";
    oss << "Content-Length: " << current_.total_bytes << "\r\n";
  } else if (!request.body.empty()) {
    if (!HttpUtils::HasHeader(merged_headers, "Content-Length")) {
      oss << "Content-Length: " << request.body.size() << "\r\n";
    }
  }

  // End of headers
  oss << "\r\n";

  // Add body for non-multipart requests
  if (!request.is_multipart() && !request.body.empty()) {
    oss << request.body;
  }

  return oss.str();
}

bool HttpClient::SendHttpRequest(const HttpRequest& request) {
  std::string request_text = BuildHttpRequest(request);

  if (!tcp_client_->Send(request_text.data(), request_text.size())) {
    return false;
  }

  // For multipart, send the body separately
  if (request.is_multipart()) {
    return SendMultipartBody(request);
  }

  return true;
}

bool HttpClient::SendMultipartBody(const HttpRequest& request) {
  const std::string& boundary = request.boundary;

  // Send multipart fields
  for (const auto& field : request.multipart_fields) {
    std::ostringstream part;
    part << "--" << boundary << "\r\n";
    StackString<128> content_disposition(
        R"(Content-Disposition: form-data; name="%s")", field.name.c_str());
    part << content_disposition.c_str() << "\r\n\r\n";
    part << field.value << "\r\n";

    std::string part_str = part.str();
    if (!tcp_client_->Send(part_str.data(), part_str.size())) {
      return false;
    }
    current_.bytes_sent += part_str.size();
  }

  // Send multipart files
  for (const auto& file : request.multipart_files) {
    std::ostringstream part_header;
    part_header << "--" << boundary << "\r\n";
    StackString<256> content_disposition(
        R"(Content-Disposition: form-data; name="%s"; filename="%s")",
        file.field_name.c_str(), file.filename.c_str());
    part_header << content_disposition.c_str() << "\r\n";
    part_header << "Content-Type: " << file.content_type << "\r\n\r\n";

    std::string header_str = part_header.str();
    if (!tcp_client_->Send(header_str.data(), header_str.size())) {
      return false;
    }
    current_.bytes_sent += header_str.size();

    // Read file content
    std::string file_content = HttpUtils::ReadFileContent(file.file_path);
    if (file_content.empty() && !HttpUtils::FileExists(file.file_path)) {
      return false;
    }

    if (!tcp_client_->Send(file_content.data(), file_content.size())) {
      return false;
    }
    current_.bytes_sent += file_content.size();

    // Report progress
    if (listener_) {
      listener_->OnProgress(this, current_.bytes_sent, current_.total_bytes);
    }

    // Send trailing CRLF
    const char* crlf = "\r\n";
    if (!tcp_client_->Send(crlf, 2)) {
      return false;
    }
    current_.bytes_sent += 2;
  }

  // Send final boundary
  std::string final_boundary = "--" + boundary + "--\r\n";
  if (!tcp_client_->Send(final_boundary.data(), final_boundary.size())) {
    return false;
  }
  current_.bytes_sent += final_boundary.size();

  return true;
}

void HttpClient::ProcessReceivedData(const void* data, size_t len) {
  current_.receive_buffer.append(static_cast<const char*>(data), len);
  current_.bytes_received += len;

  // If headers not yet received, try to parse them
  if (!current_.headers_received) {
    size_t header_end = current_.receive_buffer.find("\r\n\r\n");
    if (header_end != std::string::npos) {
      std::string header_text = current_.receive_buffer.substr(0, header_end);
      ParseResponseHeaders(header_text);
      current_.headers_received = true;

      // Remove headers from buffer
      current_.receive_buffer.erase(0, header_end + 4);

      // Check for chunked encoding or content length
      std::string transfer_encoding =
          current_.response.GetHeader("Transfer-Encoding");
      if (transfer_encoding.find("chunked") != std::string::npos) {
        current_.chunked_encoding = true;
        current_.response.chunked_encoding = true;
      }

      std::string content_length_str =
          current_.response.GetHeader("Content-Length");
      if (!content_length_str.empty()) {
        current_.content_length = std::stoull(content_length_str);
        current_.response.content_length = current_.content_length;
        if (current_.content_length > max_receive_buffer_size_) {
          streaming_mode_ = true;
        }
      }
    }
  }

  // If headers received, process body
  if (current_.headers_received) {
    // Call body data callback if in streaming mode
    if (listener_ && streaming_mode_ && !current_.receive_buffer.empty()) {
      if (!listener_->OnBodyData(this, current_.receive_buffer.data(),
                                 current_.receive_buffer.size())) {
        HandleError("failed to process body data");
      }
      current_.receive_buffer.clear();
      listener_->OnProgress(this, current_.bytes_received,
                            current_.total_bytes);
    }

    // For non-streaming mode, accumulate data
    if (!streaming_mode_ && !current_.receive_buffer.empty()) {
      current_.response.body.append(current_.receive_buffer);
      current_.receive_buffer.clear();
    }

    // Check if response is complete
    if (!current_.chunked_encoding) {
      if (current_.content_length > 0) {
        if (current_.response.body.size() >= current_.content_length) {
          // Response complete
          CompleteRequest();
        }
      } else if (current_.response.status_code >= 100 &&
                 current_.response.status_code < 200) {
        // 1xx responses don't have body
        CompleteRequest();
      } else if (current_.response.status_code == 204 ||
                 current_.response.status_code == 304) {
        // No content responses
        CompleteRequest();
      }
    } else {
      // Handle chunked encoding - simplified
      if (current_.response.body.find("0\r\n\r\n") != std::string::npos) {
        CompleteRequest();
      }
    }
  }
}

bool HttpClient::ParseHttpResponse() { return current_.headers_received; }

void HttpClient::HandleRedirect() {
  if (!follow_redirects_) {
    CompleteRequest();
    return;
  }

  if (current_.redirect_count >= max_redirects_) {
    HandleError("Too many redirects");
    return;
  }

  std::string location = current_.response.GetHeader("Location");
  if (location.empty()) {
    HandleError("Redirect response missing Location header");
    return;
  }

  if (listener_) {
    listener_->OnRedirect(this, location);
  }

  current_.redirect_count++;

  // Parse new URL
  HttpUrl new_url = current_.request.url.base();
  if (location[0] == '/') {
    new_url.path = location;
  } else {
    new_url = HttpUrl(location);
  }
  if (!new_url.IsValid()) {
    HandleError("Invalid redirect URL");
    return;
  }

  // Update request URL
  current_.request.url = new_url;

  // Reset state for new request
  current_.receive_buffer.clear();
  current_.headers_received = false;
  current_.content_length = 0;
  current_.bytes_received = 0;
  current_.response = HttpResponse();

  // Disconnect and reconnect
  if (tcp_client_->IsConnected()) tcp_client_->Disconnect();

  if (listener_) {
    listener_->OnRedirect(this, location);
  }
  current_.request.url = new_url;
  // Send new request
  if (!ConnectToHost(new_url)) {
    HandleError("failed to send request");
  }
}

void HttpClient::CompleteRequest() {
  last_response_ = current_.response;

  // Handle redirects
  if (current_.response.IsRedirect()) {
    HandleRedirect();
    return;
  }

  // Process Set-Cookie headers
  for (const auto& header : current_.response.headers) {
    if (header.name == "Set-Cookie") {
      ProcessSetCookieHeader(header.value, current_.request.url.host);
    }
  }

  // Mark as completed
  current_.completed.store(true);

  // Notify listener
  if (listener_) {
    listener_->OnHttpResponse(this, current_.response);
  }

  // Notify sync waiters
  {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    sync_cv_.notify_all();
  }

  // Reset state
  state_.store(State::kIdle);
}

void HttpClient::HandleError(const std::string& error) {
  state_.store(State::kError);
  current_.response.status_code = 0;
  current_.response.status_message = error;
  last_response_ = current_.response;

  current_.completed.store(true);

  if (listener_) {
    listener_->OnHttpError(this, error);
  }

  // Notify sync waiters
  {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    sync_cv_.notify_all();
  }

  state_.store(State::kIdle);
}

bool HttpClient::ConnectToHost(const HttpUrl& url) {
  state_ = State::kConnecting;
  return tcp_client_->Connect(url.host, url.port);
}

HttpHeaders HttpClient::MergeHeaders(const HttpHeaders& request_headers) {
  HttpHeaders merged = default_headers_;

  for (const auto& header : request_headers) {
    // Check if header already exists in default headers
    bool found = false;
    for (auto& default_header : merged) {
      if (default_header.name == header.name) {
        default_header.value = header.value;
        found = true;
        break;
      }
    }
    if (!found) {
      merged.push_back(header);
    }
  }

  return merged;
}

std::string HttpClient::FormatHeaders(const HttpHeaders& headers) {
  std::ostringstream oss;
  for (const auto& header : headers) {
    oss << header.name << ": " << header.value << "\r\n";
  }
  return oss.str();
}

void HttpClient::ParseResponseHeaders(const std::string& header_text) {
  std::istringstream stream(header_text);
  std::string line;

  // Parse status line
  if (std::getline(stream, line)) {
    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Parse "HTTP/1.1 200 OK"
    std::istringstream status_line(line);
    std::string http_version;
    status_line >> http_version >> current_.response.status_code;
    std::getline(status_line, current_.response.status_message);
    // Trim leading space
    if (!current_.response.status_message.empty() &&
        current_.response.status_message[0] == ' ') {
      current_.response.status_message.erase(0, 1);
    }
  }

  // Parse headers
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty()) {
      break;
    }

    size_t colon_pos = line.find(':');
    if (colon_pos != std::string::npos) {
      std::string name = line.substr(0, colon_pos);
      std::string value = line.substr(colon_pos + 1);

      value = TrimWhitespace(value);
      current_.response.headers.emplace_back(name, value);
    }
  }
}

void HttpClient::ProcessSetCookieHeader(const std::string& cookie_header,
                                        const std::string& domain) {
  // Simple cookie parsing: "name=value; other attributes"
  size_t eq_pos = cookie_header.find('=');
  if (eq_pos == std::string::npos) {
    return;
  }

  size_t semicolon_pos = cookie_header.find(';');
  std::string name = cookie_header.substr(0, eq_pos);
  std::string value =
      semicolon_pos != std::string::npos
          ? cookie_header.substr(eq_pos + 1, semicolon_pos - eq_pos - 1)
          : cookie_header.substr(eq_pos + 1);

  SetCookie(name, value, domain);
}

std::string HttpClient::BuildCookieHeader(const std::string& domain) {
  return GetCookies(domain);
}

std::string HttpClient::GenerateBoundary() {
  static const char alphanum[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  std::string boundary = "----XTILSFormBoundary";
  for (int i = 0; i < 16; i++) {
    boundary += alphanum[rand() % (sizeof(alphanum) - 1)];
  }
  return boundary;
}

size_t HttpClient::CalculateMultipartSize(
    const std::vector<MultipartField>& fields,
    const std::vector<MultipartFile>& files, const std::string& boundary) {
  size_t total_size = 0;

  // Calculate fields size
  for (const auto& field : fields) {
    total_size += 2 + boundary.size() + 2;  // --boundary\r\n
    total_size += 38 + field.name.size() +
                  4;  // Content-Disposition: form-data; name=""\r\n\r\n
    total_size += field.value.size() + 2;  // value\r\n
  }

  // Calculate files size
  for (const auto& file : files) {
    total_size += 2 + boundary.size() + 2;  // --boundary\r\n
    total_size += 38 + file.field_name.size() + 13 + file.filename.size() +
                  3;  // Content-Disposition header
    total_size +=
        14 + file.content_type.size() + 4;  // Content-Type: type\r\n\r\n

    // Get file size
    total_size += HttpUtils::GetFileSize(file.file_path);
    total_size += 2;  // \r\n
  }

  // Final boundary
  total_size += 2 + boundary.size() + 4;  // --boundary--\r\n

  return total_size;
}
}  // namespace xtils
