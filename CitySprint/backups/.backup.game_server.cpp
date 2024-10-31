#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

// Constants
const int BOARD_WIDTH = 800;
const int BOARD_HEIGHT = 600;
const int TILE_SIZE = 10;

// Structure to represent a tile
struct Tile {
    int x;
    int y;
    std::string color;
};

// Client structure to store client socket and other information if needed
struct Client {
    SOCKET socket;
};

// Global Game State
struct GameState {
    std::vector<std::vector<std::string>> board; // 2D board representing tile colors
    std::vector<Tile> changedTiles; // List of changed tiles
    std::mutex stateMutex;
};

GameState gameState;
std::vector<Client> clients;

// Initialize game board with empty tiles
void initializeGameState() {
    int rows = BOARD_HEIGHT / TILE_SIZE;
    int cols = BOARD_WIDTH / TILE_SIZE;
    gameState.board.resize(rows, std::vector<std::string>(cols, "black"));
    std::cout << "Game state initialized. Board size: " << rows << "x" << cols << std::endl;
}

// Function to serialize the game state into a simple string format
std::string serializeGameStateToString() {
    std::string result;
    for (const auto& tile : gameState.changedTiles) {
        result += std::to_string(tile.x) + "," + std::to_string(tile.y) + "," + tile.color + ";";
    }
    return result;
}

void sendGameStateDeltasToClients() {
    std::string gameStateStr = serializeGameStateToString();
    //std::cout << "Sending game state to clients: " << gameStateStr << std::endl;

    for (const Client& client : clients) {
        send(client.socket, gameStateStr.c_str(), gameStateStr.size(), 0);
    }

    // Clear the list of changed tiles after sending updates
    gameState.changedTiles.clear();
}

void handleClient(SOCKET clientSocket) {
    char recvBuffer[512];
    int bytesReceived;

    while ((bytesReceived = recv(clientSocket, recvBuffer, sizeof(recvBuffer), 0)) > 0) {
        std::string message(recvBuffer, bytesReceived);
        std::cout << "Received message from client: " << message << std::endl;

        {
            std::lock_guard<std::mutex> guard(gameState.stateMutex);
            // Parse the message and update game state
            int x, y;
            char color[10];
            sscanf(message.c_str(), "%d,%d,%s", &x, &y, color);
            std::cout << "Parsed values: x=" << x << ", y=" << y << ", color=" << color << std::endl;
            
            if (x < 0 || y < 0 || x >= BOARD_WIDTH / TILE_SIZE || y >= BOARD_HEIGHT / TILE_SIZE) {
                std::cerr << "Invalid tile coordinates: " << x << "," << y << std::endl;
                continue;
            }

            gameState.board[y][x] = std::string(color);
            gameState.changedTiles.push_back({x, y, std::string(color)});
        }
    }

    closesocket(clientSocket);
}

void acceptConnections(SOCKET serverSocket) {
    sockaddr_in clientAddr;
    int clientAddrSize = sizeof(clientAddr);

    while (true) {
        SOCKET clientSocket = accept(serverSocket, (SOCKADDR*)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed." << std::endl;
            continue;
        }
        clients.push_back({clientSocket});
        std::thread(handleClient, clientSocket).detach();
    }
}

void gameLoop() {
    while (true) {
        {
            std::lock_guard<std::mutex> guard(gameState.stateMutex);
            // Apply game logic and update the global game state
        }
        sendGameStateDeltasToClients();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // Roughly 60fps
    }
}

int main() {
    WSADATA wsaData;
    SOCKET ServerSocket;
    sockaddr_in serverAddr;

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    ServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(9001);

    bind(ServerSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
    listen(ServerSocket, 1);

    initializeGameState();

    std::thread acceptThread(acceptConnections, ServerSocket);
    gameLoop();

    acceptThread.join(); // Ensure the accept thread completes before exiting
    closesocket(ServerSocket);
    WSACleanup();
    return 0;
}

