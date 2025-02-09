#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <atomic>
#pragma comment(lib, "ws2_32.lib")

#define PORT "54000"
#define MAX_BUFFER_SIZE 1024


struct ClientInfo {
    SOCKET socket;
    std::string id;
    std::string nickname;
	std::chrono::steady_clock::time_point last_active;
};

std::atomic<bool> server_running(true);
std::unordered_map<std::string, ClientInfo> clients;
std::mutex clients_mutex;

void HandleClient(SOCKET client_socket);
void MonitorClients();
void BroadcastUserJoin(const std::string& userID, const std::string& nickname);
void BroadcastUserLeave(const std::string& userID);
void BroadcastUserChangeName(const std::string& userID, const std::string& newNickname);
void SendUserList(SOCKET client_socket, const std::string& selfID);

// handle client connection
void HandleClient(SOCKET client_socket) {
    char recvbuf[MAX_BUFFER_SIZE];
    ClientInfo client;

	// (1) receive handshake message (ID and nickname)
    int iResult = recv(client_socket, recvbuf, MAX_BUFFER_SIZE, 0);
    if (iResult <= 0) {
        closesocket(client_socket);
        return;
    }

    recvbuf[iResult] = '\0';
    std::string handshake(recvbuf);

	//  parse ID and nickname
    size_t id_pos = handshake.find("ID:");
    size_t nick_pos = handshake.find("NICK:");
    if (id_pos == std::string::npos || nick_pos == std::string::npos) {
        closesocket(client_socket);
        return;
    }

	client.id = handshake.substr(id_pos + 3, handshake.find('\n', id_pos) - id_pos - 3);
    client.nickname = handshake.substr(nick_pos + 5, handshake.find('\n', nick_pos) - nick_pos - 5);
    client.socket = client_socket;
	client.last_active = std::chrono::steady_clock::now();

	// add client to the list
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients[client.id] = client;
    }

	// send user list
	SendUserList(client_socket, client.id);
	//broadcast user join
	BroadcastUserJoin(client.id, client.nickname);
    
	//receive and process messages
    while (server_running) {
        iResult = recv(client_socket, recvbuf, MAX_BUFFER_SIZE, 0);
        if (iResult <= 0) break;

        recvbuf[iResult] = '\0';
        std::string msg(recvbuf);

		//update last active time
        {
			std::lock_guard<std::mutex> lock(clients_mutex);
            auto it = clients.find(client.id);
            if (it != clients.end()) {
                it->second.last_active = std::chrono::steady_clock::now();
            }
        }
		if (msg.rfind("PING", 0) == 0) continue; // ignore ping messages

		// handle different types of messages
		if (msg.find("MSG:") == 0) { // public message
            std::string broadcast_msg = "MSG:"+client.nickname + ":" + msg.substr(4);
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto& [id, c] : clients) {
                if (id != client.id) {
                    send(c.socket, broadcast_msg.c_str(), broadcast_msg.size(), 0);
                }
            }
        }
		else if (msg.find("PRIV:") == 0) { // private message
            size_t sep = msg.find('|', 5);
            std::string target_id = msg.substr(5, sep - 5);
            std::string priv_msg = "[Private]" + client.id + ":" + msg.substr(sep + 1);

            std::lock_guard<std::mutex> lock(clients_mutex);
            if (clients.find(target_id) != clients.end()) {
                send(clients[target_id].socket, priv_msg.c_str(), priv_msg.size(), 0);
            }
        }
		else if (msg.find("NICK:") == 0) { // change nickname
            client.nickname = msg.substr(5);
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients[client.id].nickname = client.nickname;
			BroadcastUserChangeName(client.id, client.nickname);
        }
    }

	//  client disconnected
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(client.id);
    }
	BroadcastUserLeave(client.id);
    closesocket(client_socket);
}
void MonitorClients() {
	while (server_running) {
		std::this_thread::sleep_for(std::chrono::seconds(60));
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(clients_mutex);

		for (auto it = clients.begin(); it != clients.end();) {
			if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_active).count() > 60) {
				
				BroadcastUserLeave(it->second.id);
                closesocket(it->second.socket);
				it = clients.erase(it);
			}
			else {
				++it;
			}
		}
	}
}
void BroadcastUserJoin(const std::string& userID, const std::string& nickname) {
    std::string joinMsg = "USERJOIN:" + userID + ":" + nickname;
    std::lock_guard<std::mutex> lock(clients_mutex);
	for (auto& [id, c] : clients) {
		if (id != userID) {
			send(c.socket, joinMsg.c_str(), joinMsg.size(), 0);
		}
	}
}
void BroadcastUserLeave(const std::string& userID) {
    std::string leftMsg = "USERLEFT:" + userID;
	std::lock_guard<std::mutex> lock(clients_mutex);
	for (auto& [id, c] : clients) {
        if (id != userID) {
            send(c.socket, leftMsg.c_str(), leftMsg.size(), 0);
        }
	}
}
void BroadcastUserChangeName(const std::string& userID, const std::string& newNickname) {
	std::string nameChangeMsg = "NICKCHANGE:" + userID + ":" + newNickname;
	//std::lock_guard<std::mutex> lock(clients_mutex);
	for (auto& [id, c] : clients) {
		if (id != userID) {
			send(c.socket, nameChangeMsg.c_str(), nameChangeMsg.size(), 0);
		}
	}
}
void SendUserList(SOCKET client_socket, const std::string& selfID) {
	std::string user_list = "USERS:";
	std::lock_guard<std::mutex> lock(clients_mutex);
	for (auto& [id, c] : clients) {
        if (id != selfID) {
            user_list += c.id + ":" + c.nickname + "\n";
        }
	}
	send(client_socket, user_list.c_str(), user_list.size(), 0);
}

int main() {
	// (1) initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

	// (2) create a socket
    addrinfo* result = nullptr, hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(nullptr, PORT, &hints, &result) != 0) {
        std::cerr << "getaddrinfo failed\n";
        WSACleanup();
        return 1;
    }

    SOCKET listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listen_socket == INVALID_SOCKET) {
        std::cerr << "socket creation failed\n";
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

	// (3) bind and listen to the socket
    if (bind(listen_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "bind failed\n";
        freeaddrinfo(result);
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }
    freeaddrinfo(result);

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed\n";
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server started on port " << PORT << std::endl;

	// (4)start monitoring thread
	std::thread(MonitorClients).detach();

	// (5) accept incoming connections
    while (server_running) {
        SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            if (server_running) std::cerr << "accept failed\n";
            break;
        }
		// handle client in a separate thread
        std::thread(HandleClient, client_socket).detach();
    }

	// (6) cleanup
    closesocket(listen_socket);
    WSACleanup();
    return 0;
}