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

std::string encodeWebSocketFrame(const std::string& message);
std::string decodeWebSocketFrame(const std::string& frame);
std::string base64Encode(const unsigned char* input, int length);
std::string generateWebSocketAcceptKey(const std::string& key);
