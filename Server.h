#ifndef SERVER_H
#define SERVER_H

#include <arpa/inet.h>
#include <map>
#include <queue>
#include <string>
#include <utility>

class TCPServerError : public std::exception
{
public:
    TCPServerError(const char* errorMessage) : message(errorMessage) {}

    const char* what() const noexcept override
    {
        return message;
    }
private:
    const char* message;
};

struct PosixThreadData;

#define MAX_CLIENTS 10

class Server
{
public:
    Server(int Port);
    ~Server();
    void startServer();
    
private:
    int serverPort;
    int serverSocket;
    struct sockaddr_in serverAddr;
    std::map<int, pthread_t> clientsMap;
    std::queue<std::pair<int, std::string>> messageQueue;
    pthread_t messageQueueThread;
    pthread_mutex_t mutex;

    void createAndBindSocket();
    void* handleMessageQueue();
    static void* handleMessageQueueWrapper(void* arg);
    void startListening();
    void* handleClient(int clientSocket);
    static void* handleClientWrapper(void* arg);
};

#endif