#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
typedef int socklen_t;
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#undef max
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
#include <limits>

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
    int id;
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

int generateUniqueId() {
    static int id = 0;
    return id++;
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

void temporarilyRemoveTroopFromGameState(Troop* troop) {
    if (!troop || troop->midpoint.size() < 2) return;

    // Temporarily set the troop's coordinates to an off-board value
    troop->midpoint[0] = -1;
    troop->midpoint[1] = -1;
}

// Come back to this immediately after refactoring to put the circle logic outside of the 
int isColliding(std::vector<int> circleOne, std::vector<int> circleTwo) {
    int xOne = circleOne[0];
    int yOne = circleOne[1];
    int radOne = circleOne[2];

    int xTwo = circleTwo[0];
    int yTwo = circleTwo[1];
    int radTwo = circleTwo[2];

    int xResult = (xTwo - xOne) * (xTwo - xOne);
    int yResult = (yTwo - yOne) * (yTwo - yOne);
    int requiredDistance = radOne + radTwo;

    int actualDistance = squareRoot(static_cast<double>(xResult + yResult));
    
    if (actualDistance < requiredDistance)
        return 1;
    return 0;
}

Troop* findNearestTroop(PlayerState& player, const std::vector<int>& coords) {
    Troop* nearestTroop = nullptr;
    int minDistance = std::numeric_limits<int>::max();

    for (auto& city : player.cities) {
        if (city.midpoint.empty()) continue; // Skip uninitialized cities
        for (auto& troop : city.troops) {
            if (troop.midpoint.size() < 2) continue; // Skip uninitialized troops
            int dx = coords[0] - troop.midpoint[0];
            int dy = coords[1] - troop.midpoint[1];
            int distance = dx * dx + dy * dy;
            if (distance < minDistance) {
                minDistance = distance;
                nearestTroop = &troop;
            }
        }
    }

    return nearestTroop;
}

bool isWithinRadius(const std::vector<int>& point, const std::vector<int>& center, int radius) {
    if (point.size() < 2 || center.size() < 2) {
        log("Invalid point or center size.");
        return false;
    }

    int dx = point[0] - center[0];
    int dy = point[1] - center[1];
    bool withinRadius = (dx * dx + dy * dy) <= (radius * radius);

    log("Checking radius: point (" + std::to_string(point[0]) + ", " + std::to_string(point[1]) +
        "), center (" + std::to_string(center[0]) + ", " + std::to_string(center[1]) +
        "), radius " + std::to_string(radius) + " -> " + (withinRadius ? "within" : "outside"));

    return withinRadius;
}

int checkCollision(const std::vector<int>& circleOne, int ignoreId = -1) {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);

    bool hasCircles = false;
    for (const auto& playerPair : gameState.player_states) {
        const PlayerState& player = playerPair.second;
        for (const auto& city : player.cities) {
            if (!city.midpoint.empty()) {
                hasCircles = true;
                break;
            }
        }
        if (hasCircles)
            break;
    }

    if (!hasCircles)
        return 0;

    for (const auto& playerPair : gameState.player_states) {
        const PlayerState& player = playerPair.second;
        for (const auto& troop : player.cities->troops) {
            if (!troop.midpoint.empty() && troop.id != ignoreId) {
                std::vector<int> circleTwo = { troop.midpoint[0], troop.midpoint[1], troop.size };
                if (isColliding(circleOne, circleTwo)) {
                    return 1;
                }
            }
        }
        for (const auto& building : player.cities->buildings) {
            if (!building.midpoint.empty()) {
                std::vector<int> circleTwo = { building.midpoint[0], building.midpoint[1], building.size };
                if (isColliding(circleOne, circleTwo)) {
                    return 1;
                }
            }
        }
        for (const auto& city : player.cities) {
            if (!city.midpoint.empty()) {
                std::vector<int> circleTwo = { city.midpoint[0], city.midpoint[1], 15 };
                if (isColliding(circleOne, circleTwo)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

// When not high as fuck, create a functional version of this using lambda functions and
// a recursive function to handle the for loop logic
int insertCharacter(std::vector<int> coords, int radius, const std::string color, int ignoreId = -1) {
    int centerX = coords[0];
    int centerY = coords[1];
    int d = 3 - 2 * radius;
    int y = radius;
    int x = 0;

    std::vector<int> circle = { coords[0], coords[1], radius };

    log("Creating a character at (" + std::to_string(centerX) + ", " + std::to_string(centerY) + ")");

    if (checkCollision(circle, ignoreId)) {
        log("Collision detected at (" + std::to_string(centerX) + ", " + std::to_string(centerY) + ")");
        return 0;
    }

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
        }
        else {
            d = d + 4 * x + 6;
        }
    }
    return 1;
}

void updateTroopMidpoint(SOCKET playerSocket, const std::vector<int>& oldMidpoint, const std::vector<int>& newMidpoint) {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);

    auto it = gameState.player_states.find(playerSocket);
    if (it == gameState.player_states.end()) {
        log("Player not found in game state.");
        return;
    }

    PlayerState& playerState = it->second;

    for (auto& city : playerState.cities) {
        for (auto& troop : city.troops) {
            if (troop.midpoint == oldMidpoint) {
                troop.midpoint = newMidpoint;
                log("Troop midpoint updated from (" + std::to_string(oldMidpoint[0]) + ", " + std::to_string(oldMidpoint[1]) + ") to (" + std::to_string(newMidpoint[0]) + ", " + std::to_string(newMidpoint[1]) + ")");
                return;
            }
        }
    }

    log("Troop with the specified midpoint not found.");
}

bool moveCharacter(SOCKET playerSocket, Troop* troopToMove, const std::vector<int>& newCoords) {
    if (!troopToMove || troopToMove->midpoint.size() < 2) {
        log("Invalid troop to move.");
        return false;
    }

    int currentX = troopToMove->midpoint[0];
    int currentY = troopToMove->midpoint[1];
    int radius = troopToMove->size;
    std::string color = troopToMove->color;
    int troopId = troopToMove->id;

    std::vector<int> currentCoords = { currentX, currentY };

    // Temporarily remove the troop from the global game state
    temporarilyRemoveTroopFromGameState(troopToMove);

    // Clear the old position on the board
    insertCharacter(currentCoords, radius, "#696969", troopId);

    // Check for collision at the new coordinates
    std::vector<int> newCircle = { newCoords[0], newCoords[1], radius };
    if (checkCollision(newCircle, troopId)) {
        log("Collision detected at (" + std::to_string(newCoords[0]) + ", " + std::to_string(newCoords[1]) + ")");
        // Reinsert the troop at the original position if collision detected
        insertCharacter(currentCoords, radius, color, troopId);
        // Restore the original coordinates in the global game state
        troopToMove->midpoint = currentCoords;
        return false;
    }

    // Insert the troop at the new position
    insertCharacter(newCoords, radius, color, troopId);

    // Update the troop's position in the global game state
    updateTroopMidpoint(playerSocket, currentCoords, newCoords);

    // Add the changed tiles to the changedTiles vector
    {
        std::lock_guard<std::mutex> lock(gameState.stateMutex);
        gameState.changedTiles.push_back({ currentX, currentY, "#696969" });
        gameState.changedTiles.push_back({ newCoords[0], newCoords[1], color });
    }

    // Send game state deltas to clients
    sendGameStateDeltasToClients();

    return true;
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
        log("Invalid message format.");
        return;
    }

    int x = std::stoi(segments[0]);
    int y = std::stoi(segments[1]);
    std::string color = segments[2];
    std::string characterType = segments[3];

    log("Parsed message: x = " + std::to_string(x) + ", y = " + std::to_string(y) + ", color = " + color + ", characterType = " + characterType);

    std::vector<int> coords = { x, y };

    if (x == 1000 && y == 1000) {
        initializeGameState();
        return;
    }

    if (player.phase == 0) {
        log("Player has no cities.");
        if (insertCharacter(coords, 15, "yellow")) {
            player.cities[0] = { { coords[0], coords[1] }, 0, 100, 10, "yellow" };
            player.phase = 1;
            update_player_state(gameState, clientSocket, player);
        }
        return;
    }

    // Find the nearest city of the player
    City* nearestCity = nullptr;
    int minDistance = std::numeric_limits<int>::max();
    for (auto& city : player.cities) {
        if (city.midpoint.empty()) continue; // Skip uninitialized cities
        int dx = coords[0] - city.midpoint[0];
        int dy = coords[1] - city.midpoint[1];
        int distance = dx * dx + dy * dy;
        if (distance < minDistance) {
            minDistance = distance;
            nearestCity = &city;
        }
    }

    Troop* nearestTroop = findNearestTroop(player, coords);
    if (nearestTroop && nearestTroop->midpoint.size() >= 2) {
        log("Nearest troop midpoint: " + std::to_string(nearestTroop->midpoint[0]) + "," + std::to_string(nearestTroop->midpoint[1]));
    }
    else {
        std::cerr << "Error: No valid nearest troop found." << std::endl;
    }

    if (!nearestCity) {
        log("No valid city found for the player.");
        return;
    }

    // Check if the action is within the allowed radius of the nearest city
    if (characterType != "select" && !isWithinRadius(coords, nearestCity->midpoint, 100)) {
        log("Action is outside the allowed radius of the city.");
        return;
    }

    if (characterType == "select") {
        std::vector<int> newCoords = { x - 1 , y };
        if (moveCharacter(clientSocket, nearestTroop, newCoords)) {
            log("Troop moved successfully");
            return;
        }
        log("failed to move troop");
        return;
    }
    // Decide what the player is trying to do now
    if (characterType == "coin") {
        if (isWithinRadius(coords, nearestCity->midpoint, 15)) {
            player.coins++;
            nearestCity->coins++;
            log(std::to_string(player.coins) + " coins collected. City now has " + std::to_string(nearestCity->coins) + " coins.");
        }
    }
    else if (characterType == "troop") {
        if (player.coins < troopMap["Barbarian"].cost) {
            log("Not enough coins to create troop.");
            return;
        }
        if (!insertCharacter(coords, troopMap["Barbarian"].size, troopMap["Barbarian"].color)) {
            log("Failed to insert troop character.");
            return;
        }
        player.coins -= troopMap["Barbarian"].cost;
        log("Troop created. Player now has " + std::to_string(player.coins) + " coins left.");

        Troop newTroop = troopMap["Barbarian"];
        newTroop.id = generateUniqueId(); // Assign a unique ID to the new troop
        newTroop.midpoint = { coords[0], coords[1] };
        nearestCity->troops.push_back(newTroop);
    }
    else if (characterType == "building") {
        if (player.coins < buildingMap["coinFarm"].cost) {
            log("Not enough coins to create building.");
            return;
        }
        if (!insertCharacter(coords, buildingMap["coinFarm"].size, buildingMap["coinFarm"].color)) {
            log("Failed to insert building character.");
            return;
        }
        player.coins -= buildingMap["coinFarm"].cost;
        log("Building created. Player now has " + std::to_string(player.coins) + " coins left.");

        Building newBuilding = buildingMap["coinFarm"];
        newBuilding.midpoint = { coords[0], coords[1] };
        nearestCity->buildings.push_back(newBuilding);
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
    player_state.phase = 0;    

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
    troopMap["Barbarian"] = { {}, {}, 3, 100, 10, 1, 1, 15, 5, "red" };

    // Buildings
    buildingMap["coinFarm"] = { {}, 5, 100, 5, 50, {}, 5, "purple" };

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
