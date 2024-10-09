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

std::unordered_map<std::string, std::string> kv_store;

int wcg = 0;
int wcp = 0;
int rcg = 0;
int rcp = 0;

static void set_nonblocking(int fd)
{
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

struct Conn
{
    int fd = -1;
    int state = 0;
    char rbuf[4096] = {0};
    // std::fill(std::begin(conn->rbuf), std::end(conn->rbuf), '\0');
    std::string response;
};

std::string handle_request(Conn *conn, std::string request, std::unordered_map <std::string, std::string> &kv_store)
{
    if (conn->fd != NULL && request[0] == 'G')
    {
        std::string key;
        int i = 5;
        while(request[i] != ' ')
        {
            key = key + request[i];
            i++;
        }
        auto it = kv_store.find(key);
        if (it != kv_store.end())
        {
            rcg++;
            std::cout << "rcg " << rcg << std::endl;
            // std::cout << send(conn->fd, conn->response.c_str(), conn->response.length(), 0) << std::endl;
            conn->response = "HTTP/1.1 200 " + it->second;
            if(send(conn->fd, conn->response.c_str(), conn->response.length(), 0) < 0)
            {
                conn->state = 2;
            }
            return conn->response;
        }
        else
        {
            wcg++;
            std::cout << "key " << key << " wcg " << wcg << std::endl;
            conn->response = "HTTP/1.1 404 Key not found";
            if(send(conn->fd, conn->response.c_str(), conn->response.length(), 0) < 0)
            {
                conn->state = 2;
            }
            return conn->response;
        }
    }
    else if (conn->fd != NULL && request[0] == 'P')
    {
        std::string key;
        int i = 6;
        while(request[i] != ' ')
        {
            key = key + request[i];
            i++;
        }
        size_t body_pos = request.find("\r\n\r\n");
        if (body_pos == std::string::npos)
        {
            wcp++;
            std::cout << "wcp no request only key " << key << " " << wcp << std::endl;
            conn->response = "HTTP/1.1 400 Invalid request";
            if(send(conn->fd, conn->response.c_str(), conn->response.length(), 0) < 0)
            {
                conn->state = 2;
            }
            return conn->response;
        }
        std::string body = request.substr(body_pos + 4);

        size_t value_pos = body.find("\"value\":");
        if (value_pos == std::string::npos)
        {
            wcp++;
            std::cout << "wcp invalid json, no value thing key " << key << wcp << std::endl;
            conn->response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid JSON";
            if(send(conn->fd, conn->response.c_str(), conn->response.length(), 0) < 0)
            {
                conn->state = 2;
            }
            return conn->response;
        }
        size_t start = body.find("\"", value_pos + 8) + 1;
        size_t end = body.find("\"", start);
        std::string value = body.substr(start, end - start);

        kv_store[key] = value;
        rcp++;
        std::cout << "key " << key << " value " << value << " rcp " << rcp << std::endl;
        conn->response = "HTTP/1.1 200 OK";
        if(send(conn->fd, conn->response.c_str(), conn->response.length(), 0) < 0)
        {
            conn->state = 2;
        }
        return conn->response;
    }
    else
    {
        conn->response = "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\n\r\nMethod not allowed";
        if(send(conn->fd, conn->response.c_str(), conn->response.length(), 0) < 0)
        {
            conn->state = 2;
        }
        return conn->response;
    }
}

static void conn_put(std::vector<Conn *> &fds, struct Conn *conn)
{
    if (fds.size() <= (size_t)conn->fd)
    {
        fds.resize(conn->fd + 1);
    }
    fds[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &con, int fd)
{
    struct sockaddr_in address = {};
    int addrlen = sizeof(address);
    int new_socket = accept(fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (new_socket < 0)
    {
        perror("accept");
        return -1;
    }
    set_nonblocking(new_socket);

    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn)
    {
        close(new_socket);
        return -1;
    }
    conn->fd = new_socket;
    conn->state = 0;
    conn_put(con, conn);
    return 0;
}

int main()
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[4096] = {0};

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 30) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    set_nonblocking(server_fd);

    std::cout << "Server listening on port 8080" << std::endl;

    std::vector<Conn *> con;
    std::vector<pollfd> fds;

    while (true)
    {
        fds.clear();
        fds.push_back({server_fd, POLLIN, 0});

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

        int rv = poll(fds.data(), fds.size(), 1);
        if (rv < 0)
        {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        for (size_t i = 1; i < fds.size(); ++i)
        {
            if (fds[i].revents)
            {
                Conn *conn = con[fds[i].fd];
                if(conn->state == 0)
                {
                    int valread = read(conn->fd, conn->rbuf, 4096);
                    if(valread == 0 || (valread < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                    {
                        std::cout << "is it here " << std::endl;
                        close(conn->fd);
                        // conn->rbuf = {0};
                        std::fill(std::begin(conn->rbuf), std::end(conn->rbuf), '\0');
                        con[conn->fd] = NULL;
                        free(conn);
                        continue;
                    }
                    else
                    {
                        std::string request(conn->rbuf);
                        conn->response = handle_request(conn, conn->rbuf, kv_store);
                        std::cout << conn->response << std::endl;
                        // std::cout << send(conn->fd, conn->response.c_str(), conn->response.length(), 0) << std::endl;

                        conn->state = 1;
                    }
                }
                if(conn->state == 1)
                {
                    conn->state = 0;
                    close(conn->fd);
                    // conn->rbuf = {0};
                    std::fill(std::begin(conn->rbuf), std::end(conn->rbuf), '\0');
                    con[conn->fd] = NULL;
                    free(conn);
                }
                if(conn->state == 2)
                {
                    close(conn->fd);
                    // conn->rbuf = {0};
                    std::fill(std::begin(conn->rbuf), std::end(conn->rbuf), '\0');
                    con[conn->fd] = NULL;
                    free(conn);
                }
            }
        }
        if (fds[0].revents)
        {
            (void)accept_new_conn(con, server_fd);
        }
    }
    return 0;
}
