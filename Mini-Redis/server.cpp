#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <string>

#include "shared.h"

std::atomic<bool> server_running(true);

#define PORT 1234
#define BUF_SIZE 1024
#define MAX_EVENTS 10

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

void send_response(int client_fd, const std::string& response) {
    size_t total = 0;
    while (total < response.size()) {
        ssize_t sent = send(client_fd, response.c_str() + total,
                            response.size() - total, 0);
        if (sent > 0) total += sent;
        else if (sent == -1 &&
                 (errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        else {
            close(client_fd);
            break;
        }
    }
}

void process_command(int client_fd, const std::string& request) {
    std::string response;

    size_t first = request.find(' ');
    std::string cmd = request.substr(0, first);
    std::string rest = (first == std::string::npos) ? "" : request.substr(first + 1);

    if (cmd == "SET") {
        size_t second = rest.find(' ');
        if (second == std::string::npos) response = "Error\n";
        else {
            std::string key = rest.substr(0, second);
            std::string value = rest.substr(second + 1);

            writer_lock_func();

            for (int i = 0; i < MAX_ENTRIES; i++) {
                if (!db->entries[i].used ||
                    strcmp(db->entries[i].key, key.c_str()) == 0) {

                    strncpy(db->entries[i].key, key.c_str(), KEY_SIZE);
                    strncpy(db->entries[i].value, value.c_str(), VALUE_SIZE);
                    db->entries[i].used = true;
                    break;
                }
            }

            writer_unlock_func();
            append_to_aof(request);

            response = "OK\n";
        }
    }

    else if (cmd == "GET") {
        reader_lock();

        bool found = false;
        for (int i = 0; i < MAX_ENTRIES; i++) {
            if (db->entries[i].used &&
                strcmp(db->entries[i].key, rest.c_str()) == 0) {

                response = std::string(db->entries[i].value) + "\n";
                found = true;
                break;
            }
        }

        reader_unlock();
        if (!found) response = "null\n";
    }

    else if (cmd == "KEYS") {
        reader_lock();

        for (int i = 0; i < MAX_ENTRIES; i++) {
            if (db->entries[i].used) {
                response += db->entries[i].key;
                response += " ";
            }
        }
        response += "\n";

        reader_unlock();
    }

	else if (cmd == "HELP") {
		response =
			"AVAILABLE COMMANDS:\n"
			"SET key value   -> store value\n"
			"GET key         -> get value\n"
			"DEL key         -> delete key\n"
			"KEYS            -> list all keys\n"
			"HELP            -> show this message\n"
			"SHUTDOWN        -> stop server\n";
	}

	else if (cmd == "SHUTDOWN") { 
		response = "Server shutting down...\n";
		send_response(client_fd, response);

		server_running = false; 
		running = false;        

		return; 
	}

    else {
        response = "Unknown command\n";
    }

    send_response(client_fd, response);
}

void handle_client(int client_fd) {
    while (true) {
        char buf[BUF_SIZE];
        ssize_t n = read(client_fd, buf, BUF_SIZE);

        if (n > 0) {
            std::string input(buf, n);

            size_t start = 0;
            while (true) {
                size_t end = input.find('\n', start);
                if (end == std::string::npos) break;

                std::string line = input.substr(start, end - start);
                process_command(client_fd, line);

                start = end + 1;
            }
        }
        else if (n == 0) {
            close(client_fd);
            break;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        else {
            close(client_fd);
            break;
        }
    }
}

int main() {
    init_shared_memory();
    init_semaphores();   
    init_aof();          
    load_aof();          

    std::thread flush_thread(aof_flush_thread);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    set_nonblocking(server_fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    int epoll_fd = epoll_create1(0);

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    epoll_event events[MAX_EVENTS];

    while (server_running) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == server_fd) {
                while (true) {
                    int client_fd = accept(server_fd, nullptr, nullptr);

                    if (client_fd == -1) {
                        if (errno == EAGAIN) break;
                        else break;
                    }

                    set_nonblocking(client_fd);

                    epoll_event cev{};
                    cev.events = EPOLLIN | EPOLLET;
                    cev.data.fd = client_fd;

                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &cev);
                }
            }
            else {
                handle_client(events[i].data.fd);
            }
        }
    }

    running = false;
    flush_thread.join();

	cleanup();

	close(server_fd);
	close(epoll_fd);

	std::cout << "Server stopped cleanly\n"; 

    return 0;
}