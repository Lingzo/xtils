#include <xtils/logging/logger.h>
#include <xtils/net/http_client.h>

#include <fstream>

#include "xtils/app/service.h"
#include "xtils/tasks/thread_task_runner.h"
#include "xtils/utils/file_utils.h"

using namespace xtils;

// Listener for downloading files
class DownloadListener : public HttpClientEventListener {
 public:
  explicit DownloadListener(const std::string& output_file = "")
      : total_received_(0) {
    if (!output_file.empty()) {
      file_.open(output_file, std::ios::binary);
      if (!file_.is_open()) {
        LogE("Failed to open output file: %s", output_file.c_str());
      }
    }
  }

  ~DownloadListener() {
    if (file_.is_open()) {
      file_.close();
    }
  }

  void OnHttpResponse(HttpClient* client,
                      const HttpResponse& response) override {
    LogI("Response: %d %s", response.status_code,
         response.status_message.c_str());
    LogI("Total bytes received: %zu", total_received_);
    if (file_.is_open()) {
      file_.close();
      LogI("File saved successfully");
    }
  }

  void OnHttpError(HttpClient* client, const std::string& error) override {
    LogE("Error: %s", error.c_str());
  }

  void OnProgress(HttpClient* client, size_t bytes_transferred,
                  size_t total_bytes) override {
    if (total_bytes > 0) {
      double progress = (bytes_transferred * 100.0 / total_bytes);
      LogI("Progress: %.1f%% (%zu/%zu)", progress, bytes_transferred,
           total_bytes);
    }
  }

  bool OnBodyData(HttpClient* client, const void* data, size_t len) override {
    total_received_ += len;
    if (file_.is_open()) {
      file_.write(static_cast<const char*>(data), len);
      LogThis();
      return file_.good();
    }
    LogThis();
    return true;
  }

 private:
  std::ofstream file_;
  size_t total_received_;
};

void PrintUsage(const char* prog) {
  printf("Usage:\n");
  printf("  %s get <url>                    - GET request\n", prog);
  printf("  %s post <url> <data>            - POST request\n", prog);
  printf("  %s download <url> <file>        - Download to file\n", prog);
  printf("  %s upload <url> <file>          - Upload file (multipart)\n", prog);
  printf("  %s async <url>                  - Async GET request\n", prog);
  printf("\nExamples:\n");
  printf("  %s get http://httpbin.org/get\n", prog);
  printf("  %s post http://httpbin.org/post '{\"key\":\"value\"}'\n", prog);
  printf("  %s download http://example.com/large.bin output.bin\n", prog);
  printf("  %s upload http://httpbin.org/post photo.jpg\n", prog);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string command = argv[1];
  std::string url = argv[2];

  auto task_runner = ThreadTaskRunner::CreateAndStart();

  if (command == "get") {
    // Simple GET request
    HttpClient client(&task_runner);
    auto response = client.Get(url);
    LogI("Status: %d %s", response.status_code,
         response.status_message.c_str());
    LogI("Body length: %zu bytes", response.body.length());
    if (response.body.length() < 1024) {
      printf("\n%s\n", response.body.c_str());
    }

  } else if (command == "post") {
    // POST request
    if (argc < 4) {
      LogE("POST requires data argument");
      PrintUsage(argv[0]);
      return 1;
    }
    std::string data = argv[3];
    HttpClient client(&task_runner);
    auto response = client.PostJson(url, data);
    LogI("Status: %d %s", response.status_code,
         response.status_message.c_str());
    LogI("Body length: %zu bytes", response.body.length());
    if (response.body.length() < 1024) {
      printf("\n%s\n", response.body.c_str());
    }

  } else if (command == "download") {
    // Download to file
    if (argc < 4) {
      LogE("Download requires output file argument");
      PrintUsage(argv[0]);
      return 1;
    }
    std::string output_file = argv[3];

    DownloadListener listener(output_file);
    HttpClient client(&task_runner, &listener);
    client.SetMaxReceiveBufferSize(1024 * 1024);  // 1MB buffer
    client.SetTimeout(300000);                    // 5 minutes for large files

    if (client.GetAsync(url)) {
      LogI("Downloading %s to %s...", url.c_str(), output_file.c_str());
      // Wait for completion
      while (client.IsBusy()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } else {
      LogE("Failed to start download");
    }

  } else if (command == "async") {
    // Async request
    DownloadListener listener;
    HttpClient client(&task_runner, &listener);

    if (client.GetAsync(url)) {
      LogI("Async request started: %s", url.c_str());
      // Wait for completion
      while (client.IsBusy()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } else {
      LogE("Failed to start async request");
    }

  } else if (command == "upload") {
    // Upload file
    if (argc < 4) {
      LogE("Upload requires file argument");
      PrintUsage(argv[0]);
      return 1;
    }
    std::string file_path = argv[3];

    // Check file exists
    if (!file_utils::exists(file_path)) {
      LogE("File not found: %s", file_path.c_str());
      return 1;
    }

    // Prepare multipart data
    std::vector<MultipartField> fields;
    fields.push_back({"description", "Uploaded via xtils http client"});

    std::vector<MultipartFile> files;
    MultipartFile file;
    file.field_name = "file";
    file.filename = file_utils::bsname(file_path);
    file.file_path = file_path;
    // Auto-detect content type based on extension
    std::string ext = file_utils::extension(file_path);
    if (ext == ".jpg" || ext == ".jpeg") {
      file.content_type = "image/jpeg";
    } else if (ext == ".png") {
      file.content_type = "image/png";
    } else if (ext == ".txt") {
      file.content_type = "text/plain";
    } else {
      file.content_type = "application/octet-stream";
    }
    files.push_back(file);

    DownloadListener listener;
    HttpClient client(&task_runner, &listener);
    client.SetTimeout(300000);  // 5 minutes for large files

    LogI("Uploading %s to %s...", file_path.c_str(), url.c_str());
    if (client.PostMultipartAsync(url, fields, files)) {
      // Wait for completion
      while (client.IsBusy()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } else {
      LogE("Failed to start upload");
    }

  } else {
    LogE("Unknown command: %s", command.c_str());
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}
