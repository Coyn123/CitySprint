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
#include <future>

#include "misc_lib.h"

// Setup cross-platform libraries above














 // Setting up our constants, function prototypes, and structures below 

// Constants
const int BOARD_WIDTH = 800;
const int BOARD_HEIGHT = 600;
const int TILE_SIZE = 2;

// Function prototypes
void handlePlayerMessage(SOCKET clientSocket, const std::string& message);
void gameLogic(SOCKET clientSocket);
void handleWebSocketHandshake(SOCKET clientSocket, const std::string& request);
void acceptPlayer(SOCKET serverSocket);
int checkCollision(const std::vector<int>& circleOne, int ignoreId);
bool isWithinRadius(const std::vector<int>& point, const std::vector<int>& center, int radius);
int isColliding(std::vector<int> circleOne, std::vector<int> circleTwo);

// Structure to represent a tile
struct Tile {
    int x{};
    int y{};
    std::string color;
};

struct CollidableEntity {
    int id;
    std::vector<int> midpoint;
    int size;
    int defense;
    int attack;
    std::vector<int> collidingEntities; // Vector to store IDs of colliding entities
    std::string color; // Add color to CollidableEntity
};

struct Troop : public CollidableEntity {
    int movement{};
    int attackDistance{};
    int cost{};
    int foodCost{};
};

struct Building : public CollidableEntity {
    int cost{};
    int food{};
    int coins{};
};

struct City : public CollidableEntity {
    int coins{};
    std::vector<Troop> troops;
    std::vector<Building> buildings;
};
// Structure to represent player state
struct PlayerState {
    SOCKET socket;
    int phase{};
    int coins{};
    City cities[2];
    std::shared_ptr<Troop> selectedTroop = nullptr; // Change to shared_ptr
};

// Global Game State
struct GameState {
    std::unordered_map<SOCKET, PlayerState> player_states;
    std::mutex mtx;
    std::vector<std::vector<std::string>> board; // 2D board representing tile colors
    std::vector<Tile> changedTiles; // List of changed tiles
    std::mutex stateMutex;
};















// Declaring our global variables for the game

GameState gameState;
std::map<SOCKET, sockaddr_in> clients;
std::ofstream logFile("./log/log.txt", std::ios::out | std::ios::app);

std::map<std::string, Troop> troopMap;
std::map<std::string, Building> buildingMap;

std::mutex clientsMutex;
std::mutex logMutex;

















void log(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
		std::cout << message << std::endl;
    logFile << message << std::endl;
}

int generateUniqueId() {
    static std::atomic<int> idCounter(1);
    int id = idCounter++;
    log("Generated unique ID: " + std::to_string(id));
    return id;
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

std::string serializePlayerStateToString(const PlayerState& player) {
    std::string result;
    result += "{\"player\": {\"coins\":\"";
    result += std::to_string(player.coins);
    result += "\",\"troops\":\"[";
    // Append the troops here
		for (auto& cities : player.cities) {
				for (auto& troop : cities.troops) {
	 					result += std::to_string(troop.id);
						result += ",";
				}
		}
		result += "FAKETROOP";
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

void initializeMaps() {
    // SETUP OUR MAPS
    // Troops
    troopMap["Barbarian"] = {
        generateUniqueId(), // id
        {0, 0}, // midpoint
        3, // size
        100, // defense
        10, // attack
        {}, // collidingEntities
        "red", // color
        1, // movement
        1, // attackDistance
        15, // cost
        5 // foodCost
    };

    // Buildings
    buildingMap["coinFarm"] = {
        generateUniqueId(), // id
        {0, 0}, // midpoint
        5, // size
        100, // defense
        5, // attack
        {}, // collidingEntities
        "purple", // color
        50, // cost
        5, // food
        0 // coins
    };
    return;
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
    }else {
        for (const auto& tile : gameState.changedTiles) {
            result += std::to_string(tile.x) + "," + std::to_string(tile.y) + "," + tile.color + ";";
        }
    }
    result += "\"}}";
    return result;
}

// Function to send game state updates to all clients
void sendGameStateDeltasToClients() {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);
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

// Some more game state functions related to moving troops
void temporarilyRemoveTroopFromGameState(Troop* troop) {
    if (!troop || troop->midpoint.size() < 2) return;

    // Temporarily set the troop's coordinates to an off-board value
    troop->midpoint[0] = -1;
    troop->midpoint[1] = -1;
}

void clearCollidingEntities() {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);

    for (auto& playerPair : gameState.player_states) {
        PlayerState& player = playerPair.second;
        for (auto& city : player.cities) {
            city.collidingEntities.clear();
            for (auto& troop : city.troops) {
                troop.collidingEntities.clear();
            }
            for (auto& building : city.buildings) {
                building.collidingEntities.clear();
            }
        }
    }
}



















// Functionality for interacting with circles, will organize more in the morning


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
        } else {
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
    } else {
        log("Invalid grid point (" + std::to_string(x) + ", " + std::to_string(y) + "). No changes made.");
        return 1;
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
        } else {
            d = d + 4 * x + 6;
        }
    }
    return 1;
}




void updateEntityMidpoint(SOCKET playerSocket, const std::vector<int>& oldMidpoint, const std::vector<int>& newMidpoint) {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);

    auto it = gameState.player_states.find(playerSocket);
    if (it == gameState.player_states.end()) {
        log("Player not found in game state.");
        return;
    }

    PlayerState& playerState = it->second;

    for (auto& city : playerState.cities) {
        if (city.midpoint == oldMidpoint) {
            city.midpoint = newMidpoint;
            return;
        }
        for (auto& troop : city.troops) {
            if (troop.midpoint == oldMidpoint) {
                troop.midpoint = newMidpoint;
                return;
            }
        }
        for (auto& building : city.buildings) {
            if (building.midpoint == oldMidpoint) {
                building.midpoint = newMidpoint;
                return;
            }
        }
    }

    log("Entity with the specified midpoint not found.");
}




void applyDamageToCollidingEntities(SOCKET playerSocket, CollidableEntity* movingEntity) {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);
    PlayerState& ourPlayer = gameState.player_states[playerSocket];

    log("Our player: " + std::to_string(ourPlayer.socket));

    log("Applying damage for moving entity " + std::to_string(movingEntity->id) + " (Client: " + std::to_string(playerSocket) + ")");

    for (auto& playerPair : gameState.player_states) {
        PlayerState& player = playerPair.second;
        log("Testing against player: " + std::to_string(playerPair.first));
        if (playerPair.first == playerSocket) {
            log("Skipping our own player.");
            continue;
        }
        for (auto& city : player.cities) {
            log("Testing against city: " + std::to_string(city.id));
            if (city.id != movingEntity->id && city.midpoint.size() >= 2 && movingEntity->midpoint.size() >= 2 && isWithinRadius(city.midpoint, movingEntity->midpoint, movingEntity->size + city.size)) {
                log("Testing against city: " + std::to_string(city.id));
                int cityDamage = movingEntity->attack;
                int movingEntityDamage = city.attack;
                city.defense -= cityDamage;
                movingEntity->defense -= movingEntityDamage;
                movingEntity->collidingEntities.push_back(city.id);
                city.collidingEntities.push_back(movingEntity->id);
                log("Entity " + std::to_string(movingEntity->id) + " (Client: " + std::to_string(playerSocket) + ") dealt " + std::to_string(cityDamage) + " damage to City " + std::to_string(city.id) + " (Client: " + std::to_string(playerPair.first) + "). City defense: " + std::to_string(city.defense));
                log("City " + std::to_string(city.id) + " (Client: " + std::to_string(playerPair.first) + ") dealt " + std::to_string(movingEntityDamage) + " damage to Entity " + std::to_string(movingEntity->id) + " (Client: " + std::to_string(playerSocket) + "). Entity defense: " + std::to_string(movingEntity->defense));
            }
            for (auto& troop : city.troops) {
                log("Testing against troop: " + std::to_string(troop.midpoint[0]) + ", " + std::to_string(troop.midpoint[1]) + " : " + std::to_string(movingEntity->midpoint[0]) + ", " + std::to_string(movingEntity->midpoint[1]));
                std::vector<int> circleOne, circleTwo;
								int newRadius = (troop.size + movingEntity->size) + 2;
                log(std::to_string(isWithinRadius(troop.midpoint, movingEntity->midpoint, newRadius)));
                if (isWithinRadius(troop.midpoint, movingEntity->midpoint, newRadius)) {
                    log("Troops are officially fighting");
                    int troopDamage = movingEntity->attack;
                    int movingEntityDamage = troop.attack;
                    troop.defense -= troopDamage;
                    movingEntity->defense -= movingEntityDamage;
                    movingEntity->collidingEntities.push_back(troop.id);
                    troop.collidingEntities.push_back(movingEntity->id);
                    log("Entity " + std::to_string(movingEntity->id) + " (Client: " + std::to_string(playerSocket) + ") dealt " + std::to_string(troopDamage) + " damage to Troop " + std::to_string(troop.id) + " (Client: " + std::to_string(playerPair.first) + "). Troop defense: " + std::to_string(troop.defense));
                    log("Troop " + std::to_string(troop.id) + " (Client: " + std::to_string(playerPair.first) + ") dealt " + std::to_string(movingEntityDamage) + " damage to Entity " + std::to_string(movingEntity->id) + " (Client: " + std::to_string(playerSocket) + "). Entity defense: " + std::to_string(movingEntity->defense));
                }
            }
            for (auto& building : city.buildings) {
                if (building.id != movingEntity->id && building.midpoint.size() >= 2 && movingEntity->midpoint.size() >= 2 && isWithinRadius(building.midpoint, movingEntity->midpoint, movingEntity->size + building.size)) {
                    int buildingDamage = movingEntity->attack;
                    int movingEntityDamage = building.attack;
                    building.defense -= buildingDamage;
                    movingEntity->defense -= movingEntityDamage;
                    movingEntity->collidingEntities.push_back(building.id);
                    building.collidingEntities.push_back(movingEntity->id);
                    log("Entity " + std::to_string(movingEntity->id) + " (Client: " + std::to_string(playerSocket) + ") dealt " + std::to_string(buildingDamage) + " damage to Building " + std::to_string(building.id) + " (Client: " + std::to_string(playerPair.first) + "). Building defense: " + std::to_string(building.defense));
                    log("Building " + std::to_string(building.id) + " (Client: " + std::to_string(playerPair.first) + ") dealt " + std::to_string(movingEntityDamage) + " damage to Entity " + std::to_string(movingEntity->id) + " (Client: " + std::to_string(playerSocket) + "). Entity defense: " + std::to_string(movingEntity->defense));
                }
            }
        }
    }
}






















// Collision logic and functions

void checkForCollidingTroops() {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);
    
    for (auto& player: gameState.player_states) {
        PlayerState& playerState = player.second;
        for (auto& cities : playerState.cities) {
            for (auto& troops : cities.troops) {
                log("Troop: " + std::to_string(troops.id));
            }
        }
    }
}

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

    int actualDistance = static_cast<int>(squareRoot(static_cast<double>(xResult + yResult)));

    log("Checking collision: circleOne (" + std::to_string(xOne) + ", " + std::to_string(yOne) + ", " + std::to_string(radOne) + "), circleTwo (" + std::to_string(xTwo) + ", " + std::to_string(yTwo) + ", " + std::to_string(radTwo) + "), actualDistance: " + std::to_string(actualDistance) + ", requiredDistance: " + std::to_string(requiredDistance));

    if (actualDistance <= requiredDistance)
        return 1;
    return 0;
}




std::shared_ptr<Troop> findNearestTroop(PlayerState& player, const std::vector<int>& coords) {
    std::shared_ptr<Troop> nearestTroop = nullptr;
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
                nearestTroop = std::make_shared<Troop>(troop);
            }
        }
    }

    return nearestTroop;
}




bool isWithinRadius(const std::vector<int>& point, const std::vector<int>& center, int radius) {
    if (point.size() < 2 || center.size() < 2) {
        log("Invalid point or center size. Point size: " + std::to_string(point.size()) + ", Center size: " + std::to_string(center.size()));
        return false;
    }

    int dx = point[0] - center[0];
    int dy = point[1] - center[1];
    int distanceSquared = dx * dx + dy * dy;
    int radiusSquared = radius * radius;
    bool withinRadius = distanceSquared <= radiusSquared;

    if (withinRadius) {
        log("Collision detected: point (" + std::to_string(point[0]) + ", " + std::to_string(point[1]) +
            "), center (" + std::to_string(center[0]) + ", " + std::to_string(center[1]) +
            "), radius " + std::to_string(radius));
    }

    return withinRadius;
}




int checkCollision(const std::vector<int>& circleOne, int ignoreId = -1) {
    std::lock_guard<std::mutex> lock(gameState.stateMutex);

    log("Checking collision for circle with ignoreId: " + std::to_string(ignoreId));

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
                    log("Collision detected between entity " + std::to_string(ignoreId) + " and troop " + std::to_string(troop.id));
                    return 1;
                }
            }
        }
        for (const auto& building : player.cities->buildings) {
            if (!building.midpoint.empty()) {
                std::vector<int> circleTwo = { building.midpoint[0], building.midpoint[1], building.size };
                if (isColliding(circleOne, circleTwo)) {
                    log("Collision detected between entity " + std::to_string(ignoreId) + " and building " + std::to_string(building.id));
                    return 1;
                }
            }
        }
        for (const auto& city : player.cities) {
            if (!city.midpoint.empty()) {
                std::vector<int> circleTwo = { city.midpoint[0], city.midpoint[1], 15 };
                if (isColliding(circleOne, circleTwo)) {
                    log("Collision detected between entity " + std::to_string(ignoreId) + " and city " + std::to_string(city.id));
                    return 1;
                }
            }
        }
    }
    return 0;
}



















// Character movement functionality below 

bool moveCharacter(SOCKET playerSocket, CollidableEntity* entityToMove, const std::vector<int>& newCoords) {
    if (!entityToMove || entityToMove->midpoint.size() < 2) {
        log("Invalid entity to move.");
        return false;
    }

    int currentX = entityToMove->midpoint[0];
    int currentY = entityToMove->midpoint[1];
    int radius = entityToMove->size;
    std::string color = entityToMove->color;
    int entityId = entityToMove->id;

    std::vector<int> currentCoords = { currentX, currentY };

    log("Moving entity " + std::to_string(entityId) + " (Client: " + std::to_string(playerSocket) + ") from (" + std::to_string(currentX) + ", " + std::to_string(currentY) + ") to (" + std::to_string(newCoords[0]) + ", " + std::to_string(newCoords[1]) + ")");

    // Check for collision at the new coordinates
    std::vector<int> newCircle = { newCoords[0], newCoords[1], radius };
    if (checkCollision(newCircle, entityId)) {
        log("Collision detected at (" + std::to_string(newCoords[0]) + ", " + std::to_string(newCoords[1]) + ")");
        return false;
    }

    // Clear the old position on the board
    insertCharacter(currentCoords, radius, "#696969", entityId);

    // Insert the entity at the new position
    insertCharacter(newCoords, radius, color, entityId);

    // Update the entity's position in the global game state
    updateEntityMidpoint(playerSocket, currentCoords, newCoords);

    // Update the entity's midpoint in the local entity object
    entityToMove->midpoint = newCoords;

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




void moveTroopToPosition(SOCKET playerSocket, std::shared_ptr<Troop> troop, const std::vector<int>& targetCoords) {
    while (troop->midpoint != targetCoords) {
        std::vector<int> currentCoords = troop->midpoint;
        std::vector<int> nextCoords = currentCoords;

        if (currentCoords[0] < targetCoords[0]) nextCoords[0]++;
        else if (currentCoords[0] > targetCoords[0]) nextCoords[0]--;

        if (currentCoords[1] < targetCoords[1]) nextCoords[1]++;
        else if (currentCoords[1] > targetCoords[1]) nextCoords[1]--;

        log("Attempting to move troop " + std::to_string(troop->id) + " (Client: " + std::to_string(playerSocket) + ") from (" + std::to_string(currentCoords[0]) + ", " + std::to_string(currentCoords[1]) + ") to (" + std::to_string(nextCoords[0]) + ", " + std::to_string(nextCoords[1]) + ")");

        // Check for collision at the next coordinates
        std::vector<int> newCircle = { nextCoords[0], nextCoords[1], troop->size };
        if (checkCollision(newCircle, troop->id)) {
            log("Collision detected at (" + std::to_string(nextCoords[0]) + ", " + std::to_string(nextCoords[1]) + ")");
            applyDamageToCollidingEntities(playerSocket, troop.get());
            return;
        }

        if (!moveCharacter(playerSocket, troop.get(), nextCoords)) {
            log("Failed to move troop " + std::to_string(troop->id) + " (Client: " + std::to_string(playerSocket) + ") to (" + std::to_string(nextCoords[0]) + ", " + std::to_string(nextCoords[1]) + ")");
            return;
        }

        // Log the updated midpoint after each move
        log("Troop " + std::to_string(troop->id) + " (Client: " + std::to_string(playerSocket) + ") moved to (" + std::to_string(troop->midpoint[0]) + ", " + std::to_string(troop->midpoint[1]) + ")");

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Adjust the delay as needed
    }
}





















// Functionality for most of the networking stuff below here


// Function to handle messages from a client
void handlePlayerMessage(SOCKET clientSocket, const std::string& message) {
    log("Handling client message: " + message);
    std::istringstream iss(message);
    std::string segment;
    std::vector<std::string> segments;

    PlayerState player = get_player_state(gameState, clientSocket);

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

    std::vector<int> coords = { x, y };

    if (x == 1000 && y == 1000) {
        initializeGameState();
        return;
    }

    if (player.phase == 0) {
        log("Player has no cities.");

        // Check if the new city is within 100 tiles of any existing city
        bool tooClose = false;
        for (const auto& playerPair : gameState.player_states) {
            const PlayerState& otherPlayer = playerPair.second;
            for (const auto& city : otherPlayer.cities) {
                if (!city.midpoint.empty()) {
                    int dx = coords[0] - city.midpoint[0];
                    int dy = coords[1] - city.midpoint[1];
                    int distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared < 100 * 100) {
                        tooClose = true;
                        break;
                    }
                }
            }
            if (tooClose) break;
        }

        if (tooClose) {
            log("Cannot create city within 100 tiles of another city.");
            return;
        }

    if (insertCharacter(coords, 15, "yellow")) {
        City newCity;
        newCity.id = generateUniqueId(); // Generate a unique ID for the city
        newCity.midpoint = { coords[0], coords[1] };
        newCity.size = 15;
        newCity.defense = 100;
        newCity.attack = 10;
        newCity.color = "yellow";
        player.cities[0] = newCity;
        player.phase = 1;
        update_player_state(gameState, clientSocket, player);
    }
        return;
    }

    if (characterType == "select") {
        std::shared_ptr<Troop> nearestTroop = findNearestTroop(player, coords);
        if (nearestTroop && isWithinRadius(coords, nearestTroop->midpoint, nearestTroop->size)) {
            player.selectedTroop = nearestTroop;
            log("Troop selected at (" + std::to_string(nearestTroop->midpoint[0]) + ", " + std::to_string(nearestTroop->midpoint[1]) + ")");
        }
        else {
            log("No troop found at the selected position.");
        }
        update_player_state(gameState, clientSocket, player);
        return;
    }

    if (characterType == "move" && player.selectedTroop) {
        std::future<void> moveFuture = std::async(std::launch::async, moveTroopToPosition, clientSocket, player.selectedTroop, coords);
				player.selectedTroop = nullptr; // Deselect the troop after starting the movement
        update_player_state(gameState, clientSocket, player);
        return;
    }

    // Check if the coordinates are within the radius of a city plus an additional 15 tiles
    bool withinCityRadius = false;
    for (const auto& city : player.cities) {
        if (isWithinRadius(coords, city.midpoint, city.size + 30)) {
            withinCityRadius = true;
            break;
        }
    }

    if (!withinCityRadius) {
        log("Cannot create " + characterType + " outside the radius of a city.");
        return;
    }

    // Existing code for handling other character types (coin, troop, building)
    if (characterType == "coin") {
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
        if (nearestCity && isWithinRadius(coords, nearestCity->midpoint, 15)) {
            player.coins++;
            nearestCity->coins++;
            log(std::to_string(player.coins) + " coins collected. City now has " + std::to_string(nearestCity->coins) + " coins.");
        }
    } else if (characterType == "troop") {
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
        if (nearestCity) {
            nearestCity->troops.push_back(newTroop);
        }
    } else if (characterType == "building") {
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
        newBuilding.id = generateUniqueId(); // Assign a unique ID to the new building
        newBuilding.midpoint = { coords[0], coords[1] };
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
        if (nearestCity) {
            nearestCity->buildings.push_back(newBuilding);
        }
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




































// This is the more global game loop running in the background, currently facing race condition problems
void boardLoop() {
    while (true) {
        clearCollidingEntities(); // Clear previous collisions

        bool hasEntitiesToProcess = false;

        {
            std::lock_guard<std::mutex> lock(gameState.stateMutex);
            for (auto& playerPair : gameState.player_states) {
                PlayerState& player = playerPair.second;
                for (auto& city : player.cities) {
                    if (!city.collidingEntities.empty()) {
                        hasEntitiesToProcess = true;
                        break;
                    }
                    for (auto& troop : city.troops) {
                        if (!troop.collidingEntities.empty()) {
                            hasEntitiesToProcess = true;
                            break;
                        }
                    }
                    for (auto& building : city.buildings) {
                        if (!building.collidingEntities.empty()) {
                            hasEntitiesToProcess = true;
                            break;
                        }
                    }
                }
                if (hasEntitiesToProcess) break;
            }
        }

        if (hasEntitiesToProcess) {
            std::lock_guard<std::mutex> lock(gameState.stateMutex);
            for (auto& playerPair : gameState.player_states) {
                PlayerState& player = playerPair.second;
                for (auto& city : player.cities) {
                    applyDamageToCollidingEntities(player.socket, &city);
                    for (auto& troop : city.troops) {
                        applyDamageToCollidingEntities(player.socket, &troop);
                    }
                    for (auto& building : city.buildings) {
                        applyDamageToCollidingEntities(player.socket, &building);
                    }
                }
            }
        }

        sendGameStateDeltasToClients();
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Adjust the delay to match troop movement speed
    }
}






























int main() {
    initializeMaps();

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

    // Use async for handling new clients and game loop
    std::future<void> acceptFuture = std::async(std::launch::async, acceptPlayer, serverSocket);
    std::future<void> boardFuture = std::async(std::launch::async, boardLoop);

    acceptFuture.get(); // Wait for accept thread to complete
    boardFuture.get();  // Wait for board loop to complete

    closesocket(serverSocket);
#ifdef _WIN32
    WSACleanup();
#endif
    logFile.close();
    return 0;
}
