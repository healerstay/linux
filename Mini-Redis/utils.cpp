#include "shared.h"
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <errno.h>
#include <atomic>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <mutex>

extern std::atomic<bool> server_running;
extern std::atomic<bool> running;
extern SharedDB* db;

#define BUF_SIZE 1024

void set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) { 
        std::cerr << "F_GETFL failed\n"; 
        exit(EXIT_FAILURE); 
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) { 
        std::cerr << "F_SETFL failed\n"; 
        exit(EXIT_FAILURE); 
    }
}

void send_response(int client_fd, const std::string& response) {
    size_t total = 0;
    while (total < response.size()) {
        ssize_t sent = send(client_fd, response.c_str() + total, response.size() - total, 0);
        if (sent > 0) total += sent;
        else if (sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
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
    std::string rest;
    if (first == std::string::npos) rest = "";
    else rest = request.substr(first + 1);

    if (cmd == "SET") {
        size_t second = rest.find(' ');
        if (second == std::string::npos) response = "Error\n";
        else {
            std::string key = rest.substr(0, second);
            std::string value = rest.substr(second + 1);

            writer_lock_func();
            for (int i = 0; i < MAX_ENTRIES; i++) {
                if (!db->entries[i].used) {
                    strncpy(db->entries[i].key, key.c_str(), KEY_SIZE);
                    strncpy(db->entries[i].value, value.c_str(), VALUE_SIZE);
                    db->entries[i].used = true;
                    break;
                } else if (strcmp(db->entries[i].key, key.c_str()) == 0) {
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
            if (db->entries[i].used && strcmp(db->entries[i].key, rest.c_str()) == 0) {
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
        response = "Server shutting down!\n";
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (int fd : clients) {
                send_response(fd, response);
            }
        }

        server_running = false;
        running = false;
        return;
    }
    else response = "Unknown command\n";

    send_response(client_fd, response);
}

void handle_client(int client_fd) {
    char buf[BUF_SIZE];
    while (true) {
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
        } else if (n == 0) { 
            close(client_fd); 
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.erase(client_fd);
            }
            break; 
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        else { 
            close(client_fd); 
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.erase(client_fd);
            }
            break; 
        }
    }
}