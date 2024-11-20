#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
typedef int socklen_t;
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#define WSAGetLastError() (errno)
#endif

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
#include <unordered_map>

#include "misc_lib.h"

// Constants
const int BOARD_WIDTH = 800;
const int BOARD_HEIGHT = 600;
const int TILE_SIZE = 2;

// Function prototypes
void handlePlayerMessage(SOCKET clientSocket, const std::string& message);
void gameLogic(SOCKET clientSocket);
void handleWebSocketHandshake(SOCKET clientSocket, const std::string& request);
void acceptPlayer(SOCKET serverSocket);

// Structure to represent a tile
struct Tile {
    int x{};
    int y{};
    std::string color;
};

// Structure to represent a troop
struct Troop {
    std::vector<int> midpoint;
    int size{};
    int defense{};
    int attack{};
    int movement{};
    int attackDistance{};
    int cost{};
    int foodCost{};
    std::string color;
};

// Structure to represent a building
struct Building {
    std::vector<int> midpoint;
    int size{};
    int defense{};
    int attack{};
    int cost{};
    int food{};
    int coins{};
    std::string color;
};

// Structure to represent a city
struct City {
    std::vector<int> midpoint;
    int coins{};
    int defense{};
    int attack{};
    std::string color;
    std::vector<Troop> troops;
    std::vector<Building> buildings;
};

// Structure to represent player state
struct PlayerState {
    SOCKET socket;
    int phase{};
    int coins{};
    City cities[2]; // Correctly declare cities as an array of City objects
};

// Global Game State
struct GameState {
    std::unordered_map<SOCKET, PlayerState> player_states;
    std::mutex mtx;
    std::vector<std::vector<std::string>> board; // 2D board representing tile colors
    std::vector<Tile> changedTiles; // List of changed tiles
    std::mutex stateMutex;
};

GameState gameState;
std::map<SOCKET, sockaddr_in> clients;
std::ofstream logFile("./log/log.txt", std::ios::out | std::ios::app);

std::map<std::string, Troop> troopMap;
std::map<std::string, Building> buildingMap;

std::mutex clientsMutex;

void log(const std::string& message) {
    std::cout << message << std::endl;
    logFile << message << std::endl;
}

void update_player_state(GameState& game_state, SOCKET socket, const PlayerState& state) {
    std::lock_guard<std::mutex> lock(game_state.mtx);
    game_state.player_states[socket] = state;
}

PlayerState get_player_state(GameState& game_state, SOCKET socket) {
    std::lock_guard<std::mutex> lock(game_state.mtx);
    return game_state.player_states[socket];
}

void remove_player(GameState& game_state, SOCKET socket) {
    std::lock_guard<std::mutex> lock(game_state.mtx);
    game_state.player_states.erase(socket);
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
            tile.color = "#696969";
            
            gameState.board[y][x] = tile.color;
            gameState.changedTiles.push_back(tile);
        }
    }
    log("Game state initialized with " + std::to_string(rows) + " rows and " + std::to_string(cols) + " columns.");
}

int drawCircle(int (*func)(int, int, const std::string), int x, int y, int radius, const std::string color) {
    int centerX = x;
    int centerY = y;
    int d = 3 - 1 * radius;
    y = radius;
    x = 0;

    while (y >= x) {
        func(centerX + x, centerY + y, color);
        func(centerX - x, centerY + y, color);
        func(centerX + x, centerY - y, color);
        func(centerX - x, centerY - y, color);
        func(centerX + y, centerY + x, color);
        func(centerX - y, centerY + x, color);
        func(centerX + y, centerY - x, color);
        func(centerX - y, centerY - x, color);

        if (d <= 0) {
            d += 4 * x + 6;
        }
        else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
    return 0;
}

int changeGridPoint(int x, int y, const std::string color) {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);

    if (x >= 0 && x < BOARD_WIDTH / TILE_SIZE && y >= 0 && y < BOARD_HEIGHT / TILE_SIZE) {
        gameState.board[y][x] = color;
        gameState.changedTiles.push_back({ x, y, color });
    }
    else {
        log("Invalid grid point (" + std::to_string(x) + ", " + std::to_string(y) + "). No changes made.");
        return 1;
    }
    return 0;
}

std::string serializePlayerStateToString(const PlayerState& player) {
    std::string result;
    result += "{\"player\": {\"coins\":\"";
    result += std::to_string(player.coins);
    result += "\",\"troops\":\"[";
    // Append the troops here
    result += "]\"}}";

    return result;
}

void sendPlayerStateDeltaToClient(const PlayerState& player) {
    std::string playerState = serializePlayerStateToString(player);
    std::string frame = encodeWebSocketFrame(playerState);

    int result = send(player.socket, frame.c_str(), static_cast<int>(frame.size()), 0);
    if (result == SOCKET_ERROR) {
        log("Failed to send update to client: " + std::to_string(WSAGetLastError()));
    }
}

// Function to serialize the game state into a simple string format
std::string serializeGameStateToString() {
    std::string result;
    result += "{\"game\": { \"board\": \"";
    if (gameState.changedTiles.empty()) {
        for (int y = 0; y < BOARD_HEIGHT / TILE_SIZE; y++) {
            for (int x = 0; x < BOARD_WIDTH / TILE_SIZE; x++) {
                result += std::to_string(x) + "," + std::to_string(y) + "," + gameState.board[y][x] + ";";
            }
        }
    } else {
        for (const auto& tile : gameState.changedTiles) {
            result += std::to_string(tile.x) + "," + std::to_string(tile.y) + "," + tile.color + ";";
        }
    }
    result += "\"}}";
    return result;
}

// Function to send game state updates to all clients
void sendGameStateDeltasToClients() { 
    if (gameState.changedTiles.empty()) {
        return;
    }
    std::string gameStateStr = serializeGameStateToString();
    std::string frame = encodeWebSocketFrame(gameStateStr);
    for (const auto& client : clients) {
        int result = send(client.first, frame.c_str(), static_cast<int>(frame.size()), 0);
        if (result == SOCKET_ERROR) {
            log("Failed to send to client: " + std::to_string(WSAGetLastError()));
        }
    }
    gameState.changedTiles.clear();
}

// Come back to this immediately after refactoring to put the circle logic outside of the 
int isColliding(int x, int y, const std::string color) {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);
    std::cout << "Made it here so far" << std::endl;
    std::cout << gameState.board[x][y] << std::endl;
    if (gameState.board[x][y] != "#696969") {
        std::cout << "DEBUG FROM HERE!!!" << std::endl;
        return 1;
    }
    return 0;
}

// When not high as fuck, create a functional version of this using lambda functions and
// a recursive function to handle the for loop logic
int insertCharacter(int coords[], int radius, const std::string color) {
    // assume the coordinates are the middle of
    // our character to create, size is the diameter  of the character
    int centerX = coords[0];
    int centerY = coords[1];
    int d = 3 - 2 * radius;
    int y = radius;
    int x = 0;

    /*
    if (drawCircle(&isColliding, x, y, radius, color)) {
        return 1;
    }
    */

    //int result = drawCircle(&changeGridPoint, x, y, radius, color);
    //std::cout << "RESULT : " << result << std::endl;
    //changeGridPoint(coords[0], coords[1], color);
    while (y >= x) {
        changeGridPoint(centerX + x, centerY + y, color);
        changeGridPoint(centerX - x, centerY + y, color);
        changeGridPoint(centerX + x, centerY - y, color);
        changeGridPoint(centerX - x, centerY - y, color);
        changeGridPoint(centerX + y, centerY + x, color);
        changeGridPoint(centerX - y, centerY + x, color);
        changeGridPoint(centerX + y, centerY - x, color);
        changeGridPoint(centerX - y, centerY - x, color);
    
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
    return 1;
}

// Function to handle messages from a client
void handlePlayerMessage(SOCKET clientSocket, const std::string& message) {
    log("Handling client message: " + message);
    std::istringstream iss(message);
    std::string segment;
    std::vector<std::string> segments;

    PlayerState player = get_player_state(gameState, clientSocket);

    log("Handling player message: " + message);

    while (std::getline(iss, segment, ',')) {
        segments.push_back(segment);
    }

    if (segments.size() != 4) {
        return;
    }

    int x = std::stoi(segments[0]);
    int y = std::stoi(segments[1]);
    std::string color = segments[2];
    std::string characterType = segments[3];

    log("Parsed message: x = " + std::to_string(x) + ", y = " + std::to_string(y) + ", color = " + color + ", characterType = " + characterType);

    int coords[2] = { x, y };

    if (x == 1000 && y == 1000) {
        initializeGameState();
    }

    if (player.phase == 0) {
        std::cout << "Player has no cities" << std::endl;
        int cityBuilt = insertCharacter(coords, 15, "yellow");
        player.cities[0] = { { coords[0], coords[1] }, 0, 100, 10, "yellow" };
        player.phase = 1;
        update_player_state(gameState, clientSocket, player);
        return;
    }

    std::string troopType = "Barbarian";

    // Decide what the player is trying to do now
    if (characterType == "coin") {
        int currentCoins = player.coins;
        player.coins = currentCoins + 1;
    } 
    if (characterType == "troop") {
        if (player.coins < troopMap["Barbarian"].cost) {
            return;
        }
        int createTroop = insertCharacter(coords, troopMap[troopType].size, troopMap[troopType].color);
        int currentCoins = player.coins;
        player.coins = currentCoins - troopMap[troopType].cost;
        std::cout << "Player has: " << player.coins << " coins left" << std::endl;

        Troop newTroop = troopMap["Barbarian"];
        newTroop.midpoint = { coords[0], coords[1] };
        player.cities->troops.push_back(newTroop);
    } 
    if (characterType == "building") {
        if (player.coins < buildingMap["coinFarm"].cost) {
            return;
        }
        int createBuilding = insertCharacter(coords, buildingMap["coinFarm"].size, buildingMap["coinFarm"].color);
        int currentCoins = player.coins;
        player.coins = currentCoins - buildingMap["coinFarm"].cost;

        Building newBuilding = buildingMap["coinFarm"];
        newBuilding.midpoint = { coords[0], coords[1] };
        player.cities->buildings.push_back(newBuilding);
    }
    update_player_state(gameState, clientSocket, player);
    sendPlayerStateDeltaToClient(player);
}

// Threaded client handling function
void gameLogic(SOCKET clientSocket) {
    char buffer[512];
    int bytesReceived;

    // Initialize player state
    PlayerState player_state;
    player_state.socket = clientSocket;
    player_state.coins = 100; // Example initial state

    // Store the initial state in the GameState structure
    update_player_state(gameState, clientSocket, player_state);

    while ((bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
        std::string message(buffer, bytesReceived);
        std::string decodedMessage = decodeWebSocketFrame(message);  // Decoding WebSocket frame
        log("Decoded message: " + decodedMessage);
        handlePlayerMessage(clientSocket, decodedMessage);
    }
    closesocket(clientSocket);
    {
        std::lock_guard<std::mutex> lock(gameState.stateMutex);
        clients.erase(clientSocket);
    }
    log("Client disconnected.");
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
void acceptPlayer(SOCKET serverSocket) {
    sockaddr_in clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);

    std::cout << "Player connected" << std::endl;
    while (true) {
        SOCKET clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET) {
            log("Accept failed: " + std::to_string(WSAGetLastError()));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(gameState.stateMutex);
            clients[clientSocket] = clientAddr; // This is where we are storing our clients
        }

        log("Client connected.");

        char buffer[1024];
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived > 0) {
            std::string request(buffer, bytesReceived);
            handleWebSocketHandshake(clientSocket, request);
            std::thread(gameLogic, clientSocket).detach();
        }
        else {
            log("Failed to receive handshake request: " + std::to_string(WSAGetLastError()));
        }
    }
}

// Game loop to handle continuous game updates
void boardLoop() {
    while (true) {
        sendGameStateDeltasToClients();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // Roughly 60fps
    }
}

int main() {
    // SETUP OUR MAPS
    // Troops
    troopMap["Barbarian"] = { {}, 3, 100, 10, 1, 1, 15, 5, "red" };

    // Buildings
    buildingMap["coinFarm"] = { {}, 5, 100, 5, 50, {}, 5, "green" };

    // NETWORK CONFIG
#ifdef _WIN32
    WSADATA wsaData;
    SOCKET serverSocket;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return 1;
    }
#else
    SOCKET serverSocket;
#endif

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(9001);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    log("Listening on port 9001.");
    initializeGameState();

    std::thread acceptThread(acceptPlayer, serverSocket);
    boardLoop(); // Keep main thread running game loop

    acceptThread.join(); // Ensure accept thread completes before exiting
    closesocket(serverSocket);
#ifdef _WIN32
    WSACleanup();
#endif
    logFile.close();
    return 0;
}
