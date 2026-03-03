#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <string>
#include <unordered_map>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 1234
#define BUF_SIZE 1024
#define MAX_EVENTS 10

std::unordered_map<std::string, std::string> db;

void set_nonblocking(int sock) {
    int opts = fcntl(sock, F_GETFL);
    if (opts < 0) {
        std::cerr << "F_GETFL failed" << std::endl;
		exit(EXIT_FAILURE);
    }
    opts = opts | O_NONBLOCK;
    if (fcntl(sock, F_SETFL, opts) < 0) {
        std::cerr << "F_SETFL failed" << std::endl;
		exit(EXIT_FAILURE);
    }
}

void send_response(int client_fd, const std::string& response) {
	size_t total_sent = 0;

	while (total_sent < response.size()) {
		ssize_t bytes_sent = send(client_fd, response.c_str() + total_sent, response.size() - total_sent, 0);
		
		if (bytes_sent > 0) total_sent 
		+= bytes_sent;
		else if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
		else {
			close(client_fd);
			break;
		}
	}
}

void process_command(int client_fd, const std::string& request) {
    std::string response;

    size_t first_space = request.find(' ');
    std::string command, rest;

    if (first_space != std::string::npos) {
        command = request.substr(0, first_space);
        rest = request.substr(first_space + 1);
    } else {
        command = request;
        rest = "";
    }

    if (command == "SET") {
        size_t second_space = rest.find(' ');
        if (second_space == std::string::npos) {
            response = "Error\n";
        } else {
            std::string key = rest.substr(0, second_space);
            std::string value = rest.substr(second_space + 1);
            db[key] = value;
            response = "OK\n";
        }
    }
    else if (command == "GET") {
        if (db.find(rest) != db.end())
            response = db[rest] + "\n";
        else
            response = "null\n";
    }
    else {
        response = "Unknown command\n";
    }

    send_response(client_fd, response);
}

void handle_client(int client_fd) {
	while (true) {
		char buf[BUF_SIZE] = {0};
		
		ssize_t bytes_read = read(client_fd, buf, BUF_SIZE);

		if (bytes_read > 0) {
			std::string input(buf, bytes_read);

			size_t start = 0;
			while(true) {
				size_t end = input.find('\n', start);
				if (end == std::string::npos) break;

				std::string line = input.substr(start, end - start);
				process_command(client_fd, line);

				start = end + 1;
			}
		} else if (bytes_read == 0) {
			close(client_fd);
			break;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			break;
		} else {
			close(client_fd);	
			break;
		}
	}
}

int main() {
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		std::cerr << "Socket failed" << std::endl;
		exit(EXIT_FAILURE);
	}

	set_nonblocking(server_fd);  

	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT);

	if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
		std::cerr << "Bind falied" << std::endl;
		exit(EXIT_FAILURE);
	}

	if (listen(server_fd, 3) < 0) {
		std::cerr << "Listen failed" << std::endl;
		exit(EXIT_FAILURE);
	}
	std::cout << "Server listening on port " << PORT << std::endl;

	int epoll_fd = epoll_create1(0);
	if (epoll_fd == -1) {
		std::cerr << "Epoll_create failed" << std::endl;
		exit(EXIT_FAILURE);
	}
	
	epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = server_fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
		std::cerr << "Epoll_ctl failed" << std::endl;
		exit(EXIT_FAILURE);
	}	
	
	epoll_event events[MAX_EVENTS];
	while(true) {
		int n_fds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (n_fds == -1) {
			std::cerr << "Epoll_wait failed" << std::endl;
                	exit(EXIT_FAILURE);
		}
		
		for (int i = 0; i < n_fds; ++i) {
			if (events[i].data.fd == server_fd) {
				while(true) {
					int client_fd = accept(server_fd, nullptr, nullptr);
					if (client_fd == -1) {
						if (errno == EAGAIN || errno == EWOULDBLOCK) {
							break;
						} else {
							std::cerr << "Accept failed" << std::endl;
							continue;
						}
					}

					set_nonblocking(client_fd);

					epoll_event client_event;
					client_event.events = EPOLLIN | EPOLLET;
					client_event.data.fd = client_fd;
					
					if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
						std::cerr << "Epoll_ctl failed for client_fd: " << client_fd << std::endl;
						close(client_fd);
					}
				}
			} else {
				handle_client(events[i].data.fd);
			}
		}
	}

	close(server_fd);
	close(epoll_fd);

	return 0;
}
