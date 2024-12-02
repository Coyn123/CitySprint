#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>

void log(const std::string& message);
int generateUniqueId();
std::string encodeWebSocketFrame(const std::string& message);
std::string decodeWebSocketFrame(const std::string& frame);
std::string base64Encode(const unsigned char* input, int length);
std::string generateWebSocketAcceptKey(const std::string& key);
double squareRoot(int initialNum);

#endif // UTILITIES_H
