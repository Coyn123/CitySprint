#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <openssl/sha.h>

#pragma comment(lib, "ws2_32.lib")

// Constants
const int BOARD_WIDTH = 800;
const int BOARD_HEIGHT = 600;
const int TILE_SIZE = 5;

// Structure to represent a tile
struct Tile {
    int x;
    int y;
    std::string color;
};

// Global Game State
struct GameState {
    std::vector<std::vector<std::string>> board; // 2D board representing tile colors
    std::vector<Tile> changedTiles; // List of changed tiles
    std::mutex stateMutex;
};

GameState gameState;
std::map<SOCKET, sockaddr_in> clients;
std::ofstream logFile("./log/log.txt", std::ios::out | std::ios::app);

void log(const std::string& message) {
    std::cout << message << std::endl;
    logFile << message << std::endl;
}

// Initialize game board with empty tiles
void initializeGameState() {
    int rows = BOARD_HEIGHT / TILE_SIZE;
    int cols = BOARD_WIDTH / TILE_SIZE;
    gameState.board.resize(rows, std::vector<std::string>(cols, "white"));

    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            Tile tile;
            tile.x = x;
            tile.y = y;
            if (y < rows / 2 - 1) {
                tile.color = "blue";  // Top half blue
            }
            else if (y > rows / 2) {
                tile.color = "brown";  // Bottom half brown
            }
            else {
                tile.color = "green";  // Middle green line
            }
            gameState.board[y][x] = tile.color;
            gameState.changedTiles.push_back(tile);
        }
    }
    log("Game state initialized with " + std::to_string(rows) + " rows and " + std::to_string(cols) + " columns.");
}

void changeGridPoint(int x, int y, const std::string& color) {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);

    if (x >= 0 && x < BOARD_WIDTH / TILE_SIZE && y >= 0 && y < BOARD_HEIGHT / TILE_SIZE) {
        gameState.board[y][x] = color;
        gameState.changedTiles.push_back({ x, y, color });
        log("Changed tile at (" + std::to_string(x) + ", " + std::to_string(y) + ") to color " + color);
    }
    else {
        log("Invalid grid point (" + std::to_string(x) + ", " + std::to_string(y) + "). No changes made.");
    }
}

// Function to encode WebSocket frames
std::string encodeWebSocketFrame(const std::string& message) {
    std::string frame;
    frame.push_back(0x81); // Text frame
    if (message.size() <= 125) {
        frame.push_back(static_cast<char>(message.size()));
    }
    else if (message.size() <= 65535) {
        frame.push_back(126);
        frame.push_back((message.size() >> 8) & 0xFF);
        frame.push_back(message.size() & 0xFF);
    }
    else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((message.size() >> (8 * i)) & 0xFF);
        }
    }
    frame.append(message);
    return frame;
}

// Function to serialize the game state into a simple string format
std::string serializeGameStateToString() {
    std::string result;
    for (const auto& tile : gameState.changedTiles) {
        result += std::to_string(tile.x) + "," + std::to_string(tile.y) + "," + tile.color + ";";
    }
    //log("Serialized game state: " + result);
    return result;
}

// Function to send game state updates to all clients
void sendGameStateDeltasToClients() {
    /*if (gameState.changedTiles.empty()) {
        // If no changes, send at least a keep-alive ping (optional)
        log("No changes detected. Sending keep-alive ping.");
        std::string frame = encodeWebSocketFrame("ping");
        for (const auto& client : clients) {
            int result = send(client.first, frame.c_str(), static_cast<int>(frame.size()), 0);
            if (result == SOCKET_ERROR) {
                log("Failed to send keep-alive ping to client: " + std::to_string(WSAGetLastError()));
            }
        }
        return;
    }*/
    std::string gameStateStr = serializeGameStateToString();
    //log("Game state being sent: " + gameStateStr);
    std::string frame = encodeWebSocketFrame(gameStateStr);
    //log("Sending game state deltas to clients: " + gameStateStr);
    for (const auto& client : clients) {
        int result = send(client.first, frame.c_str(), static_cast<int>(frame.size()), 0);
        if (result == SOCKET_ERROR) {
            log("Failed to send to client: " + std::to_string(WSAGetLastError()));
        }
    }
    //gameState.changedTiles.clear();
}

std::string decodeWebSocketFrame(const std::string& frame) {
    size_t payloadStart = 2;
    size_t payloadLength = frame[1] & 0x7F;

    if (payloadLength == 126) {
        payloadStart = 4;
        payloadLength = (frame[2] << 8) | frame[3];
    }
    else if (payloadLength == 127) {
        payloadStart = 10;
        payloadLength = 0;
        for (int i = 0; i < 8; i++) {
            payloadLength |= (static_cast<size_t>(frame[2 + i]) << (56 - 8 * i));
        }
    }

    std::string decoded;
    if (frame[1] & 0x80) { // Masked
        char masks[4] = { frame[payloadStart], frame[payloadStart + 1], frame[payloadStart + 2], frame[payloadStart + 3] };
        size_t i = payloadStart + 4;
        size_t j = 0;
        while (j < payloadLength) {
            decoded.push_back(frame[i] ^ masks[j % 4]);
            i++;
            j++;
        }
    }
    else {
        decoded = frame.substr(payloadStart, payloadLength);
    }
    return decoded;
}

// Function to handle messages from a client
void handleClientMessage(SOCKET clientSocket, const std::string& message) {
    log("Handling client message: " + message);
    std::istringstream iss(message);
    int x, y;
    std::string color;
    char delimiter;

    if (iss >> x >> delimiter >> y >> delimiter >> color) {
        std::lock_guard<std::mutex> lock(gameState.stateMutex);
        if (x >= 0 && x < BOARD_WIDTH / TILE_SIZE && y >= 0 && y < BOARD_HEIGHT / TILE_SIZE) {
            gameState.board[y][x] = color;
            gameState.changedTiles.push_back({ x, y, color });
            log("Updated tile at (" + std::to_string(x) + ", " + std::to_string(y) + ") to color " + color);
        }
        else {
            log("Invalid grid point (" + std::to_string(x) + ", " + std::to_string(y) + "). No changes made.");
        }
    }
    else {
        log("Failed to parse client message: " + message);
    }
}

// Threaded client handling function
void handleClient(SOCKET clientSocket) {
    char buffer[512];
    int bytesReceived;
    while ((bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
        std::string message(buffer, bytesReceived);
        log("Received message from client: " + message);
        std::string decodedMessage = decodeWebSocketFrame(message);  // Decoding WebSocket frame
        log("Decoded message: " + decodedMessage);
        handleClientMessage(clientSocket, decodedMessage);
    }
    closesocket(clientSocket);
    {
        std::lock_guard<std::mutex> lock(gameState.stateMutex);
        clients.erase(clientSocket);
    }
    log("Client disconnected.");
}

// Base64 encoding function
std::string base64Encode(const unsigned char* input, int length) {
    static const char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    for (int i = 0; i < length; i += 3) {
        int val = (input[i] << 16) + (i + 1 < length ? (input[i + 1] << 8) : 0) + (i + 2 < length ? input[i + 2] : 0);
        encoded.push_back(base64Chars[(val >> 18) & 0x3F]);
        encoded.push_back(base64Chars[(val >> 12) & 0x3F]);
        encoded.push_back(i + 1 < length ? base64Chars[(val >> 6) & 0x3F] : '=');
        encoded.push_back(i + 2 < length ? base64Chars[val & 0x3F] : '=');
    }
    return encoded;
}

// Generate WebSocket accept key
std::string generateWebSocketAcceptKey(const std::string& key) {
    std::string acceptKey = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(acceptKey.c_str()), acceptKey.size(), hash);
    return base64Encode(hash, SHA_DIGEST_LENGTH);
}

// Handle WebSocket handshake
// Function to handle WebSocket handshake
void handleWebSocketHandshake(SOCKET clientSocket, const std::string& request) {
    std::istringstream requestStream(request);
    std::string line;
    std::string webSocketKey;

    while (std::getline(requestStream, line) && line != "\r") {
        if (line.find("Sec-WebSocket-Key") != std::string::npos) {
            webSocketKey = line.substr(line.find(":") + 2);
            webSocketKey = webSocketKey.substr(0, webSocketKey.length() - 1);
        }
    }

    std::string acceptKey = generateWebSocketAcceptKey(webSocketKey);
    std::ostringstream responseStream;
    responseStream << "HTTP/1.1 101 Switching Protocols\r\n";
    responseStream << "Upgrade: websocket\r\n";
    responseStream << "Connection: Upgrade\r\n";
    responseStream << "Sec-WebSocket-Accept: " << acceptKey << "\r\n\r\n";
    std::string response = responseStream.str();

    send(clientSocket, response.c_str(), static_cast<int>(response.size()), 0);

    log("Handshake response sent: " + response);

    // Send initial game state after handshake
    std::string gameStateStr = serializeGameStateToString();
    std::string frame = encodeWebSocketFrame(gameStateStr);
    int sendResult = send(clientSocket, frame.c_str(), static_cast<int>(frame.size()), 0);
    if (sendResult == SOCKET_ERROR) {
        log("Failed to send initial state to client: " + std::to_string(WSAGetLastError()));
    }
    else {
        log("Initial game state sent to client.");
    }
}

// Accept new client connections
void acceptConnections(SOCKET serverSocket) {
    sockaddr_in clientAddr;
    int clientAddrSize = sizeof(clientAddr);

    while (true) {
        SOCKET clientSocket = accept(serverSocket, (SOCKADDR*)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET) {
            log("Accept failed: " + std::to_string(WSAGetLastError()));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(gameState.stateMutex);
            clients[clientSocket] = clientAddr;
        }

        log("Client connected.");

        char buffer[1024];
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived > 0) {
            std::string request(buffer, bytesReceived);
            handleWebSocketHandshake(clientSocket, request);
            std::thread(handleClient, clientSocket).detach();
        }
        else {
            log("Failed to receive handshake request: " + std::to_string(WSAGetLastError()));
        }
    }
}

// Game loop to handle continuous game updates
void gameLoop() {
    while (true) {
        {
            std::lock_guard<std::mutex> guard(gameState.stateMutex);
            // Apply game logic and update the global game state
            // Ensure no game logic here is preventing updates
        }
        sendGameStateDeltasToClients();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // Roughly 60fps
    }
}

int main() {
    WSADATA wsaData;
    SOCKET serverSocket;
    sockaddr_in serverAddr;

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(9001);

    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        log("Bind failed: " + std::to_string(WSAGetLastError()));
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        log("Listen failed: " + std::to_string(WSAGetLastError()));
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    log("Listening on port 9001.");
    initializeGameState();

    std::thread acceptThread(acceptConnections, serverSocket);
    gameLoop(); // Keep main thread running game loop

    acceptThread.join(); // Ensure accept thread completes before exiting
    closesocket(serverSocket);
    WSACleanup();
    logFile.close();
    return 0;
}
