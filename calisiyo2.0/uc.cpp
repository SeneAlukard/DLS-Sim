#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#define GBS_PORT 8080

std::atomic<bool> isLeader(false); // Track if this drone is the leader
int droneID = -1;                  // Assigned by gBS
int currentLeaderID = -1;
int rssi = 0; // Current RSSI level

void handleGBSMessages(int sock) {
  char buffer[1024] = {0};

  while (true) {
    int bytesRead = read(sock, buffer, 1024);
    if (bytesRead <= 0) {
      std::cout << "Disconnected from gBS.\n";
      close(sock);
      break;
    }

    std::string message(buffer, bytesRead);

    if (message.find("ID") != std::string::npos) {
      // Assign ID
      droneID = std::stoi(message.substr(3));
      std::cout << "Assigned ID: " << droneID << "\n";
    } else if (message.find("ROLE_CHANGE") != std::string::npos) {
      if (message.find("LEADER") != std::string::npos) {
        std::cout << "Promoted to Leader.\n";
        isLeader.store(true);
      } else if (message.find("FOLLOWER") != std::string::npos) {
        currentLeaderID =
            std::stoi(message.substr(message.find_last_of(' ') + 1));
        std::cout << "Demoted to Follower. Current Leader: " << currentLeaderID
                  << "\n";
        isLeader.store(false);
      }
    } else if (message == "STATUS_CHECK") {
      std::cout << "Received STATUS_CHECK from gBS.\n";
      // Send STATUS_OK with current RSSI level
      std::string response = "STATUS_OK " + std::to_string(rssi);
      send(sock, response.c_str(), response.size(), 0);
      std::cout << "Sent response: " << response << "\n";
    }
  }
}

int main() {
  int sock = 0;
  struct sockaddr_in serv_addr;
  char buffer[1024] = {0};

  // Connect to gBS
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    std::cout << "Socket creation error\n";
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(GBS_PORT);

  if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
    std::cout << "Invalid address/Address not supported\n";
    return -1;
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    std::cout << "Connection to gBS failed\n";
    return -1;
  }

  // Assign random RSSI level
  std::srand(std::time(0) ^
             std::hash<std::thread::id>{}(std::this_thread::get_id()));
  rssi = -75 - (rand() % 36); // Generate a value between -75 and -110
  std::cout << "Random RSSI Level: " << rssi << "\n";

  // Send RSSI level to gBS
  std::string data = std::to_string(rssi);
  send(sock, data.c_str(), data.size(), 0);

  // Handle messages from gBS
  std::thread(handleGBSMessages, sock).detach();

  // Simulate RSSI changes over time
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    if (rssi > 0) {
      rssi = -75 - (rand() % 36); // Generate a value between -75 and -110
    }
  }

  return 0;
}
