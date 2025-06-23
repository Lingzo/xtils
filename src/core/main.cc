#include <chrono>
#include <thread>

#include "xtils/core/http_server.h"
#include "xtils/logging/logger.h"
#include "xtils/tasks/thread_task_runner.h"

namespace xtils {
class WebHandler : public xtils::HttpRequestHandler {
  void OnHttpRequest(const HttpRequest& req) {
    if (req.is_websocket_handshake) {
      req.conn->UpgradeToWebsocket(req);
      return;
    }
    if (req.uri.ToStr() == "/basic") {
      if (auto auth = req.GetHeader("Authorization")) {
        LogI("Authorization: %s", auth->ToStr().c_str());
        req.conn->SendResponseAndClose("200 OK", {}, "OK");
        return;
      }
      req.conn->SendResponse("401 No Auth",
                             {{"WWW-Authenticate", "Basic realm=\"xtils\""}});
      return;
    }
    LogI("method: %s \nuri: %s \n%s", req.method.ToStr().c_str(),
         req.uri.ToStr().c_str(), req.body.ToStr().c_str());
    static int i = 0;
    i++;
    std::string str = "This is teste " + std::to_string(i);
    req.conn->SendResponseAndClose("200", {{"Content-Type", "text/html"}},
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
}  // namespace xtils
int main(int argc, char* argv[]) {
  auto thread_task_runner = xtils::ThreadTaskRunner::CreateAndStart("main");
  xtils::WebHandler handle;
  xtils::HttpServer server(&thread_task_runner, &handle);
  server.AddAllowedOrigin("*");
  server.Start(9090);
  while (true) {
    thread_task_runner.PostTask([]() { LogThis(); });
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  return 0;
}
