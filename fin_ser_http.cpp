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

static void set_nonblocking(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno)
    {
        int err = errno;
        const char *msg = "fcntl error";
        fprintf(stderr, "[%d] %s\n", err, msg);
        std::cout << "its the non-bl abort 1 " << err << std::endl;
        abort();
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno)
    {
        int err = errno;
        const char *msg = "fcntl error";
        fprintf(stderr, "[%d] %s\n", err, msg);
        std::cout << "its the non-bl abort 2 " << err << std::endl;
        abort();
        return;
    }
}

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,  // mark the connection for deletion
};

struct Conn {
    int fd = -1;
    int state = 0;     // either STATE_REQ or STATE_RES
    // buffer for reading
    // size_t rbuf_size = 0;
    // uint8_t rbuf[4 + k_max_msg];
    char rbuf[4096] = {0};
    // buffer for writing
    // char wbuf[4096] = {0};
    std::string response;
    // size_t wbuf_size = 0;
    // size_t wbuf_sent = 0;
    // uint8_t wbuf[4 + k_max_msg];
};

// std::string handle_request(const std::string& request, std::unordered_map<std::string, std::string>& kv_store) {
// std::string handle_request(std::string request, std::unordered_map <std::string, std::string> &kv_store) {
std::string handle_request(Conn *conn, std::string request, std::unordered_map <std::string, std::string> &kv_store) {

    // if (method == "GET") {
    if (conn->fd != NULL && request[0] == 'G') {
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
    }
    else if (conn->fd != NULL && request[0] == 'P')
    {
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
            std::cout << "wcp no request only key " << key << " " << wcp << std::endl;
            return "HTTP/1.1 400 Invalid request";
        }
        std::string body = request.substr(body_pos + 4);

        // Extract value from JSON (very basic parsing)
        size_t value_pos = body.find("\"value\":");
        if (value_pos == std::string::npos) {
            wcp++;
            std::cout << "wcp invalid json, no value thing key " << key << wcp << std::endl;
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid JSON";
        }
        size_t start = body.find("\"", value_pos + 8) + 1;
        size_t end = body.find("\"", start);
        std::string value = body.substr(start, end - start);

        kv_store[key] = value;
        // return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK";
        rcp++;
        // std::cout << request << std::endl;
        std::cout << "key " << key << " value " << value << " rcp " << rcp << std::endl;
        return "HTTP/1.1 200 OK";
    }
    else
    {
        if(conn->fd != NULL)
            return "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\n\r\nMethod not allowed";
    }
}

static void conn_put(std::vector<Conn *> &fds, struct Conn *conn) {
    // std::cout << "conn_put size of fds before resizing " << fds.size() << std::endl;
    if (fds.size() <= (size_t)conn->fd) {
        fds.resize(conn->fd + 1);
    }
    fds[conn->fd] = conn;
    // std::cout << "conn_put func " << conn->fd << std::endl;
}

static int32_t accept_new_conn(std::vector<Conn *> &con, int fd) {
    // accept
    struct sockaddr_in address = {};
    int addrlen = sizeof(address);
    // socklen_t socklen = sizeof(client_addr);
    // int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    // if (connfd < 0) {
    //     msg("accept() error");
    //     return -1;  // error
    // }

    int new_socket = accept(fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (new_socket < 0) {
        perror("accept");
        return -1;
        // continue;
    }
    set_nonblocking(new_socket);
    // con.push_back({new_socket, POLLIN, 0});

    // set the new connection fd to nonblocking mode
    // fd_set_nb(connfd);
    // creating the struct Conn
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn) {
        close(new_socket);
        return -1;
    }
    conn->fd = new_socket;
    conn->state = 0;
    // conn->rbuf_size = 0;
    // conn->wbuf_size = 0;
    // conn->wbuf_sent = 0;
    conn_put(con, conn);
    // std::cout << "accepting conns " << std::endl;
    return 0;
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

    // struct timeval tv;
    // tv.tv_sec = 8;
    // Forcefully attaching socket to the port 8080
    // if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // if (setsockopt(server_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
    //     perror("setsockopt SO_KEEPALIVE failed");
    //     exit(EXIT_FAILURE);
    // }

    // if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv)) < 0) {
    //     perror("setsockopt SO_RCVTIMEO failed");
    //     exit(EXIT_FAILURE);
    // }

    // if (setsockopt(server_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv)) < 0) {
    //     perror("setsockopt SO_SNDTIMEO failed");
    //     exit(EXIT_FAILURE);
    // }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 30) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // newc
    set_nonblocking(server_fd);

    std::cout << "Server listening on port 8080" << std::endl;

    std::vector<Conn *> con;
    // newc
    std::vector<pollfd> fds;
    // fds.push_back({server_fd, POLLIN, 0});

    while (true) {
            // int poll_count = poll(fds.data(), fds.size(), -1);
            fds.clear();
            fds.push_back({server_fd, POLLIN, 0});

            // if (poll_count == -1) {
            //     perror("poll");
            //     exit(EXIT_FAILURE);
            // }

            for (Conn *conn : con)
            {
                if (!conn)
                {
                    continue;
                }
                struct pollfd pfd = {};
                pfd.fd = conn->fd;
                pfd.events = (conn->state == 0) ? POLLIN : POLLOUT;
                pfd.events = pfd.events | POLLERR;
                fds.push_back(pfd);
            }

            // int rv = poll(fds.data(), (nfds_t)fds.size(), 1000);
            int rv = poll(fds.data(), fds.size(), 1);
            if (rv < 0)
            {
                perror("poll");
                exit(EXIT_FAILURE);
            }

            for (size_t i = 1; i < fds.size(); ++i) {
                // if (fds[i].revents & POLLIN) {
                // std::cout << "here first aftest " << std::endl;
                if (fds[i].revents) {
                    // std::cout << server_fd << " here first bef " << fds[i].fd << std::endl;
                    // if (fds[i].fd == server_fd)
                    // {
                        // std::cout << "here first " << std::endl;
                        // New connection
                        Conn *conn = con[fds[i].fd];
                        if(conn->state == 0)
                        {
                            int valread = read(conn->fd, conn->rbuf, 4096);
                            if(valread == 0 || (valread < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                            {
                                std::cout << "is it here " << std::endl;
                                close(conn->fd);
                                con[conn->fd] = NULL;
                                free(conn);
                                continue;
                            }
                            else
                            {
                                std::string request(conn->rbuf);
                                // conn->response = handle_request(request, kv_store);
                                conn->response = handle_request(conn, conn->rbuf, kv_store);
                                std::cout << conn->response << std::endl;
                                std::cout << send(conn->fd, conn->response.c_str(), conn->response.length(), 0) << std::endl;

                                conn->state = 1;
                                // state_res(conn);
                            }
                        }
                        if(conn->state == 1)
                        {
                            // send(conn->fd, conn->response.c_str(), conn->response.length(), 0);
                            conn->state = 0;
                            // close(conn->fd);

                            // (void)
                            close(conn->fd);
                            con[conn->fd] = NULL;
                            free(conn);
                        }
                        // else
                        // {

                        // }
                        // int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
                        // if (new_socket < 0) {
                        //     perror("accept");
                        //     continue;
                        // }
                        // set_nonblocking(new_socket);
                        // fds.push_back({new_socket, POLLIN, 0});
                    // }
                    // else
                    // {
                    //     // Existing connection - handle request
                    //     // char buffer[4096] = {0};
                    //     int valread = read(fds[i].fd, buffer, 4096);
                    //     if (valread <= 0)
                    //     {
                    //         // Connection closed or error
                    //         close(fds[i].fd);
                    //         fds.erase(fds.begin() + i);
                    //         i--; // Adjust index after erasing
                    //     }
                    //     else
                    //     {
                    //         std::string request(buffer);
                    //         std::string response = handle_request(request, kv_store);
                    //         send(fds[i].fd, response.c_str(), response.length(), 0);
                    //         close(fds[i].fd);
                    //         fds.erase(fds.begin() + i);
                    //         i--;
                    //     }
                    // }
                }
            }
            if (fds[0].revents) {
                // std::cout << "accepting conns " << std::endl;
                // std::cout << "server_fd " << server_fd << std::endl;
                (void)accept_new_conn(con, server_fd);
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
