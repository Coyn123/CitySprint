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

#include "misc_lib.h"

#pragma comment(lib, "ws2_32.lib")

// Constants
const int BOARD_WIDTH = 800;
const int BOARD_HEIGHT = 600;
const int TILE_SIZE = 2;

// Structure to represent a tile
struct Tile {
    int x{};
    int y{};
    std::string color;
};

// Global Game State
struct GameState {
    std::vector<std::vector<std::string>> board; // 2D board representing tile colors
    std::vector<Tile> changedTiles; // List of changed tiles
    std::mutex stateMutex;
};

struct Troop {
    // Location Tracker
    std::vector<int> midpoint;
    int size{};
    
    // Basic troop attributes
    int defense{};
    int attack{};
    int movement{};
    int attackDistance{};
    int cost{};

    // Troops also have a cost to maintain
    int foodCost{};

    // Troops will have a designated color
    std::string color;
};

struct Building {
    // Location Tracker
    std::vector<int> midpoint;    
    int size{};

    // Each building can be attacked and will attack withing a melee attack radius 
    int defense{};
    int attack{};
    int cost{};
    
    // Buildings are either coin or food production, so if food == 0 -> coins
    int food{};
    int coins{};   
 
    std::string color;
};

struct City {
    // Location Tracker
    //std::vector<std::vector<int>> coordinates;
    std::vector<int> midpoint;
    int coins{};

    // Basic city attributes
    int defense{};
    int attack{};
    std::string color;
    
    // Store our cities troops and buildings for those troops
    std::vector<Troop> troops;
    std::vector<Building> buildings;
};

struct PlayerState { // This is you
    SOCKET socket;

    int phase{};
    int coins{};
    City cities[2]; 
};

GameState gameState;
std::map<SOCKET, sockaddr_in> clients;
std::ofstream logFile("./log/log.txt", std::ios::out | std::ios::app);

thread_local PlayerState player;

std::map<std::string, Troop> troopMap;
std::map<std::string, Building> buildingMap;

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
            tile.color = "#696969";
            
            gameState.board[y][x] = tile.color;
            gameState.changedTiles.push_back(tile);
        }
    }
    log("Game state initialized with " + std::to_string(rows) + " rows and " + std::to_string(cols) + " columns.");
}

void changeGridPoint(int x, int y, const std::string& color) {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);
   /* 
    auto isColliding = [](int x, int y, const GameState& gs) {
        if (gs.board[x][y] != "#696969") {
            std::cout << gs.board[x][y] << " Is HITTING -> " << "#696969" << std::endl;
            return 1;
        }
        std::cout << gs.board[x][y] << " Is NOT HITTING -> " << "#696969" << std::endl;
        return 0;
    };

    if (isColliding(x,y, gameState)) {
        return;
    }
*/   
    if (x >= 0 && x < BOARD_WIDTH / TILE_SIZE && y >= 0 && y < BOARD_HEIGHT / TILE_SIZE) {
        gameState.board[y][x] = color;
        gameState.changedTiles.push_back({ x, y, color });
    } else {
        log("Invalid grid point (" + std::to_string(x) + ", " + std::to_string(y) + "). No changes made.");
    }
}

std::string serializePlayerStateToString() {
    std::string result;
    result += "{\"player\": {\"coins\":\"";
    result += std::to_string(player.coins);
    result += "\",\"troops\":\"[";
    // Append the troops here
    result += "]\"}}";

    return result;
}

void sendPlayerStateDeltaToClient() {
    std::string playerState = serializePlayerStateToString();
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
		for (int y = 0; y < BOARD_HEIGHT/TILE_SIZE; y++) {
		    for (int x = 0; x < BOARD_WIDTH/TILE_SIZE; x++) {
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

int drawCircle(int (*func)(int,int), int x, int y, int radius, std::string color) {
    return func(x,y);
}

// Come back to this immediately after refactoring to put the circle logic outside of the 
int isColliding(int x, int y) {
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
int insertCharacter(int coords[], int radius, std::string color) {
    // assume the coordinates are the middle of
    // our character to create, size is the diameter  of the character
    int centerX = coords[0];
    int centerY = coords[1];
    int d = 3 - 2 * radius;
    int y = radius;
    int x = 0;

    if (drawCircle(&isColliding, x, y, radius, color)) {
        return 1;
    }

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
		player.cities[0] = { { coords[0], coords[1] }, 0, 100, 10, "yellow"};
		player.phase = 1;
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
		newTroop.midpoint = {coords[0], coords[1]};
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
    sendPlayerStateDeltaToClient();
}

// Threaded client handling function
void gameLogic(SOCKET clientSocket) {
    char buffer[512];
    int bytesReceived;
    player.socket = clientSocket;

    // This is where we initialize the player for their instance of the game 
    player.coins = 100;   
    
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
    int clientAddrSize = sizeof(clientAddr);

    while (true) {
        SOCKET clientSocket = accept(serverSocket, (SOCKADDR*)&clientAddr, &clientAddrSize);
        if (clientSocket == INVALID_SOCKET) {
            log("Accept failed: " + std::to_string(WSAGetLastError()));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(gameState.stateMutex);
            clients[clientSocket] = clientAddr; // This is where we are storing our clients
            player.socket = clientSocket;
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
    troopMap["Barbarian"] = { {}, 3, 100, 10, 1, 1, 15, 5, "red"};

    // Buildings
    buildingMap["coinFarm"] = { {}, 5, 100, 5, 50, {}, 5, "green"};

    // NETWORK CONFIG
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

    std::thread acceptThread(acceptPlayer, serverSocket);
    boardLoop(); // Keep main thread running game loop

    acceptThread.join(); // Ensure accept thread completes before exiting
    closesocket(serverSocket);
    WSACleanup();
    logFile.close();
    return 0;
}
