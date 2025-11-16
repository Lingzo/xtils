#include <xtils/logging/logger.h>
#include <xtils/net/http_client.h>

#include "xtils/app/service.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;

int main(int argc, char **argv) {
  auto task_runner = ThreadTaskRunner::CreateAndStart();
  HttpClient client(&task_runner);
  std::string url = "http://httpbin.org/get";
  if (argc > 1) {
    url = argv[1];
  }
  auto response = client.Get(url);
  LogI("Status: %d.", response.status_code);
  LogI("Body: %s.", response.body.c_str());
  task_runner.PostTask([]() { LogI("Task executed."); });
}
