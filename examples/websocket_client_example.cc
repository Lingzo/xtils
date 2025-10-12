#include <iostream>
#include <sstream>
#include <string>

#include "xtils/net/websocket_client.h"
#include "xtils/tasks/thread_task_runner.h"

using namespace xtils;

class ExampleWebSocketListener : public WebSocketClientEventListener {
 public:
  void OnWebSocketConnected(WebSocketClient* client) override {
    std::cout << "WebSocket connected to: " << client->GetUrl() << std::endl;

    // Send a test message
    client->SendText("Hello WebSocket Server!");

    // Send a binary message
    std::string binary_data = "Binary data example";
    client->SendBinary(binary_data.data(), binary_data.size());

    // Send a ping
    client->SendPing("ping data");
  }

  void OnWebSocketMessage(WebSocketClient* client,
                          const WebSocketMessage& message) override {
    std::cout << "Received " << (message.is_text ? "text" : "binary")
              << " message (" << message.data.size() << " bytes): ";

    if (message.is_text) {
      std::cout << message.data << std::endl;
    } else {
      std::cout << "[binary data]" << std::endl;
    }
  }

  void OnWebSocketClosed(WebSocketClient* client, uint16_t code,
                         const std::string& reason) override {
    std::cout << "WebSocket closed with code " << code;
    if (!reason.empty()) {
      std::cout << ", reason: " << reason;
    }
    std::cout << std::endl;
  }

  void OnWebSocketError(WebSocketClient* client,
                        const std::string& error) override {
    std::cout << "WebSocket error: " << error << std::endl;
  }

  void OnWebSocketPing(WebSocketClient* client,
                       const std::string& data) override {
    std::cout << "Received ping: " << data << std::endl;
  }

  void OnWebSocketPong(WebSocketClient* client,
                       const std::string& data) override {
    std::cout << "Received pong: " << data << std::endl;
  }
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " <websocket_url>" << std::endl;
    std::cout << "Example: " << argv[0] << " ws://localhost:8080/websocket"
              << std::endl;
    return 1;
  }

  std::string url = argv[1];

  // Create task runner (simplified - in real app you'd have proper event loop)
  auto task_runner = ThreadTaskRunner::CreateAndStart();  // You'd create a real
                                                          // task runner here

  // Create WebSocket client listener
  ExampleWebSocketListener listener;

  // Create WebSocket client
  WebSocketClient client(&task_runner, &listener);

  // Configure client
  client.SetMaxMessageSize(1024 * 1024);  // 1MB max message size
  client.SetPingInterval(30000);          // Ping every 30 seconds
  client.SetAutoReconnect(true, 5000);    // Auto-reconnect with 5 second delay

  // Add custom headers if needed
  HttpHeaders headers;
  headers.emplace_back("Authorization", "Bearer your-token-here");
  headers.emplace_back("User-Agent", "WebSocketClient-Example/1.0");

  // Specify protocols if needed
  std::vector<std::string> protocols = {"chat", "echo"};

  // Connect to WebSocket server
  std::cout << "Connecting to: " << url << std::endl;

  if (!client.Connect(url, headers, protocols)) {
    std::cout << "Failed to initiate WebSocket connection" << std::endl;
    return 1;
  }

  // In a real application, you'd run an event loop here
  // For this example, we'll just wait for user input
  std::cout << "WebSocket client started. Commands:" << std::endl;
  std::cout << "  text <message>    - Send text message" << std::endl;
  std::cout << "  binary <message>  - Send binary message" << std::endl;
  std::cout << "  ping [data]       - Send ping frame" << std::endl;
  std::cout << "  close [code] [reason] - Close connection" << std::endl;
  std::cout << "  quit              - Exit program" << std::endl;

  std::string input;
  while (std::getline(std::cin, input)) {
    if (input == "quit" || input == "exit") {
      break;
    }

    std::istringstream iss(input);
    std::string command;
    iss >> command;

    if (command == "text") {
      std::string message;
      std::getline(iss, message);
      if (!message.empty() && message[0] == ' ') {
        message.erase(0, 1);  // Remove leading space
      }

      if (client.IsConnected()) {
        client.SendText(message);
        std::cout << "Sent text: " << message << std::endl;
      } else {
        std::cout << "Not connected" << std::endl;
      }

    } else if (command == "binary") {
      std::string message;
      std::getline(iss, message);
      if (!message.empty() && message[0] == ' ') {
        message.erase(0, 1);
      }

      if (client.IsConnected()) {
        client.SendBinary(message);
        std::cout << "Sent binary: " << message << std::endl;
      } else {
        std::cout << "Not connected" << std::endl;
      }

    } else if (command == "ping") {
      std::string data;
      std::getline(iss, data);
      if (!data.empty() && data[0] == ' ') {
        data.erase(0, 1);
      }

      if (client.IsConnected()) {
        client.SendPing(data);
        std::cout << "Sent ping: " << data << std::endl;
      } else {
        std::cout << "Not connected" << std::endl;
      }

    } else if (command == "close") {
      uint16_t code = WebSocketCloseCode::kNormalClosure;
      std::string reason;

      iss >> code;
      std::getline(iss, reason);
      if (!reason.empty() && reason[0] == ' ') {
        reason.erase(0, 1);
      }

      if (client.IsConnected()) {
        client.Close(code, reason);
        std::cout << "Closing connection..." << std::endl;
      } else {
        std::cout << "Not connected" << std::endl;
      }

    } else if (command == "status") {
      std::cout << "Connection state: ";
      switch (client.GetState()) {
        case WebSocketClient::State::kDisconnected:
          std::cout << "Disconnected";
          break;
        case WebSocketClient::State::kConnecting:
          std::cout << "Connecting";
          break;
        case WebSocketClient::State::kHandshaking:
          std::cout << "Handshaking";
          break;
        case WebSocketClient::State::kConnected:
          std::cout << "Connected";
          break;
        case WebSocketClient::State::kClosing:
          std::cout << "Closing";
          break;
        case WebSocketClient::State::kClosed:
          std::cout << "Closed";
          break;
        case WebSocketClient::State::kError:
          std::cout << "Error";
          break;
      }
      std::cout << std::endl;

      if (!client.GetProtocol().empty()) {
        std::cout << "Selected protocol: " << client.GetProtocol() << std::endl;
      }

    } else {
      std::cout << "Unknown command: " << command << std::endl;
    }
  }

  // Clean shutdown
  if (client.IsConnected()) {
    std::cout << "Closing WebSocket connection..." << std::endl;
    client.Close();

    // In a real app, you'd wait for the close confirmation
    // For this example, we'll just wait a bit
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  std::cout << "Example completed." << std::endl;
  return 0;
}
