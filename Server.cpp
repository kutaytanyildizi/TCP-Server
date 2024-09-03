#include "Server.h" // Including the header file to define the Server class and related errors
#include <iostream> // Including the library for standard input/output operations
#include <cstring> // This header file is included to use C-style string manipulation functions like memset
#include <unistd.h> // This header file is included for POSIX operating system API, which is used for functions like usleep
#include <sys/socket.h> // This header file is included for socket-related functions and structures used in network programming, 
                        // such as socket, bind, listen, and accept

#define STATIC

// Structure to hold data for POSIX threads
struct PosixThreadData
{
    PosixThreadData(Server* srv, int clientSocket) : server(srv), clientSoc(clientSocket) {}
    Server* server; // Pointer to a Server object
    int clientSoc; // Client socket descriptor
};

// Constructor for the Server class, taking a port number as argument
Server::Server(int Port) : serverPort(Port)
{   
    try
    {
        createAndBindSocket(); // Creating and binding the socket for the server

        // Creating a thread for handling the message queue
        if(pthread_create(&messageQueueThread, NULL, handleMessageQueueWrapper, (void *)this) != 0)
        {
            throw TCPServerError("Message Queue Thread could not be created."); // Throw an error if thread creation fails
        }
    }
    catch(const TCPServerError& ex)
    {
        close(serverSocket); // Clean up resources in case of exception
        throw; // Re-throw the exception
    }
}

// Destructor for the Server class
Server::~Server()
{
    // Join threads for all connected clients
    for(auto& [socket, thread]: clientsMap)
    {
        if(pthread_join(thread, NULL) != 0)
        {
            std::cerr << "Failed to join thread.\n"; // Print error message if joining thread fails
        }

        close(socket);
    }
  
    if(pthread_join(messageQueueThread, NULL) != 0)
    {
        std::cerr << "Failed to join thread.\n"; // Print error message if joining thread fails
    }
  
    close(serverSocket); // Closing the server socket when the Server object is destroyed
}

// Function to create and bind the socket for the server
void Server::createAndBindSocket()
{
    close(serverSocket); // Closing any existing socket before creating and binding a new one

    // Creating a socket for communication using IPv4 and TCP protocol
    if((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        throw TCPServerError("Socket could not be created."); // Throw an error if socket creation fails
    }

    // Initializing the server address structure
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET; // Using IPv4
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Accepting connections from any IP address
    serverAddr.sin_port = htons(serverPort); // Setting the server port

    // Binding the socket to the server address
    if(bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        close(serverSocket);
        throw TCPServerError("Unable to bind socket."); // Throw an error if binding fails
    }
}

// Function to start the server
void Server::startServer()
{
    bool dec = true; // Decision variable
    
    // Main loop to start and restart the server
    while(dec)
    {
        try
        {
            startListening(); // Starting to listen for incoming connections
        }
        catch(const TCPServerError ex)
        {
            std::cerr << ex.what() << '\n'; // Printing error message if an exception is caught
        }
        
        std::cout << "Do you want to try to start the server again? [Y/N]";
        std::string ans{}; // Answer
        int counter = 0;
        while(ans != "Y" && ans != "y" && ans != "N" && ans != "n")    
        {
            if(counter >= 1)
            {
                std::cout << "Invalid answer, please enter your answer again: ";
            }
            std::cin >> ans; // Getting user input for restarting the server
            ++counter;
        }
        
        if(ans == "Y" || ans == "y")
        {
            dec = true; // Setting decision to true for restarting the server
            clientsMap.clear(); // Clearing the map of connected clients
            createAndBindSocket(); // Recreating and binding the socket 
        }
        else
        {
            dec = false; // Setting decision to false for shutting down the server
            std::cout << "Server is shutting down.\n"; // Printing shutdown message
        }
    }  
}

// Function to handle the message queue in a separate thread
void* Server::handleMessageQueue()
{
    while(true)
    {
        while(messageQueue.empty())
            usleep(10000); // Sleep for 10ms

        // Retrieve and process messages from the message queue
        pthread_mutex_lock(&mutex); // Lock mutex before accessing shared resources
        std::pair<int, std::string> clientMessagePair = messageQueue.front();
        messageQueue.pop();
        pthread_mutex_unlock(&mutex); // Unlock mutex after accessing shared resources
        std::cout << "Message from Client " << clientMessagePair.first << " : " << clientMessagePair.second;
    }
}

// Static function wrapper for handling the message queue in a separate thread
STATIC void* Server::handleMessageQueueWrapper(void* arg)
{
    Server* instance = reinterpret_cast<Server*>(arg);
    instance->handleMessageQueue(); // Call the non-static member function to handle the message queue
}

// Function to start listening for incoming connections
void Server::startListening()
{
    if (listen(serverSocket, MAX_CLIENTS) == -1) 
    {
        throw TCPServerError("Listening error."); // Throw an error if listening fails
    }

    std::cout << "Server is listening for connections on Port " << serverPort << "\n"; // Print listening message

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    // Accept incoming client connections and spawn threads to handle them
    while(true)
    {
        int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if(clientSocket == -1)
        {
            std::cerr << "Failed to accept request from a client." << "\n"; // Print error message if accepting client fails
        }
        else
        {
            std::cout << "Client " << clientSocket << " connected.\n"; // Print client connection message
        }

        pthread_t thread;
        PosixThreadData threadData(this, clientSocket); // Create thread data structure
        if(pthread_create(&thread, NULL, handleClientWrapper, (void *)&threadData) != 0)
        {
            throw TCPServerError("Thread could not be created."); // Throw an error if thread creation fails
            close(clientSocket); // Close client socket if thread creation fails
        }
        else
        {
            clientsMap[clientSocket] = thread; // Add client socket and thread to the map
        }
    }
}

// Function to handle a client connection
void* Server::handleClient(int clientSocket)
{
    char buffer[256]; // Buffer to store received data
    int bytesRead = 0; // Number of bytes read

    // Receive data from the client until connection is closed
    while((bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        pthread_mutex_lock(&mutex); // Lock mutex before accessing shared resources
        messageQueue.push(std::make_pair(clientSocket, buffer)); // Push received data to the message queue
        pthread_mutex_unlock(&mutex); // Unlock mutex after accessing shared resources
        memset(buffer, 0, sizeof(buffer)); // Clear the buffer
    }

    if(bytesRead <= 0)
    {
        std::cout << "Client " << clientSocket << " disconnected." << "\n";
    }

    close(clientSocket);
}

// Static function wrapper for handling client connections
STATIC void* Server::handleClientWrapper(void* arg)
{
    PosixThreadData* threadData = reinterpret_cast<PosixThreadData*>(arg);
    threadData->server->handleClient(threadData->clientSoc);
}
