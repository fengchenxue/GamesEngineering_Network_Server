#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iostream>

#pragma comment(lib, "ws2_32.lib") 

#define PORT "54000"  

int main() {
    WSADATA wsaData;
    SOCKET listenSocket = INVALID_SOCKET, clientSocket = INVALID_SOCKET;
    struct addrinfo* result = NULL, hints;

    
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }


    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE; 

    if (getaddrinfo(NULL, PORT, &hints, &result) != 0) {
        std::cerr << "getaddrinfo failed.\n";
        WSACleanup();
        return 1;
    }

   
    listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Error creating socket.\n";
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

 
    if (bind(listenSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Bind failed.\n";
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

   
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed.\n";
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is running, waiting for connections...\n";

  
    clientSocket = accept(listenSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Accept failed.\n";
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Client connected!\n";

    
    char recvbuf[512];
    int recvbuflen = 512;
    int iResult;

    do {
        iResult = recv(clientSocket, recvbuf, recvbuflen, 0);
        if (iResult > 0) {
            recvbuf[iResult] = '\0';
            std::cout << "Received: " << recvbuf << std::endl;
            send(clientSocket, "Message received", 16, 0);
        }
    } while (iResult > 0);

    
    closesocket(clientSocket);
    closesocket(listenSocket);
    WSACleanup();

    return 0;
}
