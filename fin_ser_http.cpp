// #include "crow_all.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <netinet/tcp.h>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <fcntl.h>
#include <sys/select.h>
#include <algorithm>
#include <poll.h>

// working, just have to implement event-driven and non-blocking IO.

std::unordered_map<std::string, std::string> kv_store;

int wcg = 0;
int wcp = 0;
int rcg = 0;
int rcp = 0;

// newc
void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

// std::string handle_request(const std::string& request, std::unordered_map<std::string, std::string>& kv_store) {
std::string handle_request(std::string request, std::unordered_map <std::string, std::string> &kv_store) {

    // if (method == "GET") {
    if (request[0] == 'G') {
        // Extract key from path
        // std::string key = path.substr(1);  // Remove leading '/'
        std::string key;
        // key = "";
        int i = 5;
        while(request[i] != ' ')
        {
            key = key + request[i];
            i++;
        }
        auto it = kv_store.find(key);
        if (it != kv_store.end()) {
            // return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n" + it->second;
            rcg++;
            std::cout << "rcg " << rcg << std::endl;
            return "HTTP/1.1 200 " + it->second;
        } else {
            // return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nKey not found";
            wcg++;
            std::cout << "key " << key << " wcg " << wcg << std::endl;
            return "HTTP/1.1 404 Key not found";
        }
    // } else if (method == "POST") {
    } else if (request[0] == 'P') {
        // Extract key from path
        // std::string key = path.substr(1);  // Remove leading '/'

        std::string key;
        int i = 6;
        while(request[i] != ' ')
        {
            key = key + request[i];
            i++;
        }

        // Find the JSON body
        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos == std::string::npos) {
            // return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid request";
            wcp++;
            std::cout << "wcp " << wcp << std::endl;
            return "HTTP/1.1 400 Invalid request";
        }
        std::string body = request.substr(body_pos + 4);

        // Extract value from JSON (very basic parsing)
        size_t value_pos = body.find("\"value\":");
        if (value_pos == std::string::npos) {
            wcp++;
            std::cout << "wcp " << wcp << std::endl;
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid JSON";
        }
        size_t start = body.find("\"", value_pos + 8) + 1;
        size_t end = body.find("\"", start);
        std::string value = body.substr(start, end - start);

        kv_store[key] = value;
        // return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK";
        rcp++;
        std::cout << "key " << key << " value " << value << " rcp " << rcp << std::endl;
        return "HTTP/1.1 200 OK";
    } else {
        return "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\n\r\nMethod not allowed";
    }
}

int main() {
    // std::unordered_map<std::string, std::string> kv_store;

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[4096] = {0};

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    // if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 300) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // newc
    set_nonblocking(server_fd);

    std::cout << "Server listening on port 8080" << std::endl;

    // newc
    std::vector<pollfd> fds;
    fds.push_back({server_fd, POLLIN, 0});

    while (true) {
            int poll_count = poll(fds.data(), fds.size(), -1);

            if (poll_count == -1) {
                perror("poll");
                exit(EXIT_FAILURE);
            }

            for (size_t i = 0; i < fds.size(); i++) {
                if (fds[i].revents & POLLIN) {
                    if (fds[i].fd == server_fd) {
                        // New connection
                        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
                        if (new_socket < 0) {
                            perror("accept");
                            continue;
                        }
                        set_nonblocking(new_socket);
                        fds.push_back({new_socket, POLLIN, 0});
                    } else {
                        // Existing connection - handle request
                        // char buffer[4096] = {0};
                        int valread = read(fds[i].fd, buffer, 4096);
                        if (valread <= 0)
                        {
                            // Connection closed or error
                            close(fds[i].fd);
                            fds.erase(fds.begin() + i);
                            i--; // Adjust index after erasing
                        }
                        else
                        {
                            std::string request(buffer);
                            std::string response = handle_request(request, kv_store);
                            send(fds[i].fd, response.c_str(), response.length(), 0);
                            close(fds[i].fd);
                            fds.erase(fds.begin() + i);
                            i--;
                        }
                    }
                }
            }
        }

        return 0;
    }

    // while(true) {
    //     if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
    //         perror("accept");
    //         std::cout << "did you die?" << std::endl;
    //         exit(EXIT_FAILURE);
    //     }


    //     int valread = read(new_socket, buffer, 4096);
    //     std::string request(buffer);

    //     // Process the request
    //     std::string response = handle_request(request, kv_store);

    //     // std::cout << response << std::endl;
    //     send(new_socket, response.c_str(), response.length(), 0);
    //     close(new_socket);
    // }

//     return 0;
// }
