#include <iostream>
#include <string>
#include <fstream>
#include <openssl/sha.h>

#pragma comment(lib, "ws2_32.lib")

std::string encodeWebSocketFrame(const std::string& message);
std::string decodeWebSocketFrame(const std::string& frame);
std::string base64Encode(const unsigned char* input, int length);
std::string generateWebSocketAcceptKey(const std::string& key);
double squareRoot(int num);
