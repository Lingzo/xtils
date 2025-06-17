#include <chrono>
#include <thread>

#include "assert.h"
#include "http_server.h"
#include "logger.h"
#include "thread_task_runner.h"

namespace base {
class WebHandler : public base::HttpRequestHandler {
  void OnHttpRequest(const HttpRequest& req) {
    LogI("method: %s \nuri: %s \n%s", req.method.ToStr().c_str(),
         req.uri.ToStr().c_str(), req.body.ToStr().c_str());
    static int i = 0;
    i++;
    std::string str = "This is teste " + std::to_string(i);
    req.conn->SendResponseAndClose("200", {{"Content-Type", mime::TEXT_HTML}},
                                   str.c_str());
    CHECK(false);
  }
  void OnWebsocketMessage(const WebsocketMessage& ws) {
    auto str = ws.data.ToStr();
    LogI("is_text: %d,%s", ws.is_text, str.c_str());
    ws.conn->SendWebsocketMessageText(str.data(), str.size());
  }
  void OnHttpConnectionClosed(HttpServerConnection* con) {
    LogI("%s", "closed socket!!!!");
  }
};
}  // namespace base
int main(int argc, char* argv[]) {
  auto thread_task_runner = base::ThreadTaskRunner::CreateAndStart("main");
  base::WebHandler handle;
  base::HttpServer server(&thread_task_runner, &handle);
  server.AddAllowedOrigin("*");
  server.Start(9090);
  while (true) {
    thread_task_runner.PostTask([]() { LogThis(); });
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  return 0;
}
