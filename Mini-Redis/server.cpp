#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <cstring>

#define PORT 1234
#define BUF_SIZE 1024
#define MAX_EVENTS 10

void handle_client(int client_fd) {
	char buf[BUF_SIZE] = {0};
	
	int bytes_read = read(client_fd, buf, BUF_SIZE);
	if (bytes_read <= 0) {
		close(client_fd);
		return;	
	}

	std::cout << "Received: " << buf << std::endl;

	std::string response = "OK\n";
	send(client_fd, response.c_str(), response.size(), 0);

	close(client_fd);
}

int main() {
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		std::cerr << "Socket failed" << std::endl;
		exit(EXIT_FAILURE);
	}

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
	event.events = EPOLLIN;
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
				int client_fd = accept(server_fd, nullptr, nullptr);
				if (client_fd == -1) {
					std::cerr << "Accept failed" << std::endl;
					continue;
				}	

				epoll_event client_event;
               			client_event.events = EPOLLIN;
                		client_event.data.fd = client_fd;
                		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);
			} else {
				handle_client(events[i].data.fd);
				epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, nullptr);
			}
		}
	}

	close(server_fd);
	close(epoll_fd);

	return 0;
}

