#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define PORT 8080
#define MAX_DRONES 3
#define STATUS_INTERVAL 5 // Send STATUS_CHECK every 5 seconds
#define SOCKET_TIMEOUT 2  // 2 seconds for socket send/recv timeouts

struct DroneData {
  int id;
  int battery;
  bool isLeader;
  int socket; // Store socket for communication
};

std::vector<DroneData> drones;
std::mutex droneMutex; // Use std::mutex for synchronization
int nextDroneID = 1;   // To assign unique IDs

void printDrones() {
  std::lock_guard<std::mutex> lock(droneMutex);
  std::cout << "[DEBUG] Current Drones:\n";
  for (const auto &drone : drones) {
    std::cout << "  ID: " << drone.id << ", Battery: " << drone.battery
              << ", IsLeader: " << (drone.isLeader ? "Yes" : "No") << "\n";
  }
}

void notifyLeaderChange(int newLeaderID) {
  std::cout << "[DEBUG] Entering notifyLeaderChange(), New Leader ID: "
            << newLeaderID << "\n";

  std::vector<DroneData> dronesCopy;
  {
    std::lock_guard<std::mutex> lock(droneMutex);
    dronesCopy = drones;
    for (auto &drone : dronesCopy) {
      drone.isLeader = (drone.id == newLeaderID);
    }
  }

  for (auto &drone : dronesCopy) {
    std::string message =
        drone.isLeader ? "ROLE_CHANGE LEADER"
                       : "ROLE_CHANGE FOLLOWER " + std::to_string(newLeaderID);
    if (send(drone.socket, message.c_str(), message.size(), 0) <= 0) {
      std::cerr << "[ERROR] Failed to send ROLE_CHANGE to Drone " << drone.id
                << ". Closing socket and removing drone.\n";

      close(drone.socket);
      {
        std::lock_guard<std::mutex> lock(droneMutex);
        drones.erase(std::remove_if(drones.begin(), drones.end(),
                                    [&drone](const DroneData &d) {
                                      return d.id == drone.id;
                                    }),
                     drones.end());
      }
    }
  }

  std::cout << "[DEBUG] Exiting notifyLeaderChange()\n";
}

void selectLeader() {
  std::cout << "[DEBUG] Entering selectLeader()\n";

  std::vector<DroneData> dronesCopy;
  {
    std::lock_guard<std::mutex> lock(droneMutex);

    if (drones.empty()) {
      std::cout << "[INFO] No drones connected. Skipping leader selection.\n";
      return;
    }

    dronesCopy = drones;
  }

  int maxBattery = -1;
  int leaderID = -1;

  for (const auto &drone : dronesCopy) {
    if (drone.battery > maxBattery) {
      maxBattery = drone.battery;
      leaderID = drone.id;
    }
  }

  if (leaderID != -1) {
    notifyLeaderChange(leaderID);
  }

  std::cout << "[DEBUG] Exiting selectLeader()\n";
}

void statusCheckThread() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(STATUS_INTERVAL));

    std::vector<DroneData> dronesCopy;
    {
      std::lock_guard<std::mutex> lock(droneMutex);
      dronesCopy = drones; // Copy drones to avoid holding mutex during sends
    }

    for (auto &drone : dronesCopy) {
      try {
        std::string message = "STATUS_CHECK";
        std::string response;

        std::cout << "[DEBUG] Sending STATUS_CHECK to Drone ID: " << drone.id
                  << "\n";

        if (send(drone.socket, message.c_str(), message.size(), 0) <= 0) {
          throw std::runtime_error("Send failed, drone disconnected.");
        }

        char buffer[1024] = {0};
        int recvBytes = recv(drone.socket, buffer, sizeof(buffer), 0);

        if (recvBytes <= 0) {
          throw std::runtime_error("Receive failed, drone disconnected.");
        }

        response = std::string(buffer, recvBytes);
        std::cout << "[DEBUG] Received response from Drone ID: " << drone.id
                  << ": " << response << "\n";

      } catch (const std::exception &e) {
        std::cerr << "[ERROR] Drone " << drone.id
                  << " disconnected: " << e.what() << "\n";

        bool wasLeader = false;

        {
          std::lock_guard<std::mutex> lock(droneMutex);

          // Check if the disconnected drone was the leader
          for (const auto &d : drones) {
            if (d.id == drone.id && d.isLeader) {
              wasLeader = true;
              break;
            }
          }

          // Remove disconnected drone
          drones.erase(std::remove_if(drones.begin(), drones.end(),
                                      [&drone](const DroneData &d) {
                                        return d.id == drone.id;
                                      }),
                       drones.end());
        }

        if (wasLeader) {
          std::cout << "[INFO] Leader Drone " << drone.id
                    << " disconnected. Selecting a new leader.\n";
          selectLeader();
        }
      }
    }
  }
}

void handleNewConnection(int clientSocket) {
  char buffer[1024] = {0};

  int bytesRead = read(clientSocket, buffer, 1024);
  if (bytesRead <= 0) {
    std::cerr << "[ERROR] Failed to read battery level from drone.\n";
    close(clientSocket);
    return;
  }

  int battery = std::stoi(buffer);

  DroneData newDrone;
  {
    std::lock_guard<std::mutex> lock(droneMutex);
    newDrone.id = nextDroneID++;
    newDrone.battery = battery;
    newDrone.isLeader = false;
    newDrone.socket = clientSocket;

    // Set socket timeouts
    struct timeval timeout;
    timeout.tv_sec = SOCKET_TIMEOUT;
    timeout.tv_usec = 0;

    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
               sizeof(timeout));
    setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &timeout,
               sizeof(timeout));

    drones.push_back(newDrone);
  }

  std::string idMessage = "ID " + std::to_string(newDrone.id);
  if (send(clientSocket, idMessage.c_str(), idMessage.size(), 0) <= 0) {
    std::cerr << "[ERROR] Failed to send ID to Drone " << newDrone.id << ".\n";
    close(clientSocket);
    return;
  }
  std::cout << "[INFO] Assigned ID " << newDrone.id
            << " to a drone with battery " << battery << "%\n";

  printDrones();
  selectLeader();
}

int main() {
  int server_fd, new_socket;
  struct sockaddr_in address;
  int addrlen = sizeof(address);

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("[ERROR] Socket creation failed");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("[ERROR] Bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, MAX_DRONES) < 0) {
    perror("[ERROR] Listen failed");
    exit(EXIT_FAILURE);
  }

  std::cout << "[INFO] gBS Server listening on port " << PORT << "...\n";

  // Start the STATUS_CHECK thread
  std::thread(statusCheckThread).detach();

  while (true) {
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                             (socklen_t *)&addrlen)) >= 0) {
      std::cout << "[INFO] New drone connected.\n";
      std::thread(handleNewConnection, new_socket).detach();
    }
  }

  return 0;
}
