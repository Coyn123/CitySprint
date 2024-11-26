#include <iostream>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <functional>
#include <ctime>
#include <iomanip>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
typedef int socklen_t;
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#pragma comment(lib, "Ws2_32.lib")
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

#include "server.h"

std::unordered_map<std::string, std::string> mime_types;
std::unordered_map<std::string, std::function<std::string(const std::string&)>> get_routes;
std::unordered_map<std::string, std::function<std::string(const std::string&)>> post_routes;

void initialize_mime_types() {
  mime_types[".html"] = "text/html";
  mime_types[".css"] = "text/css";
  mime_types[".js"] = "application/javascript";
  mime_types[".png"] = "image/png";
  mime_types[".jpg"] = "image/jpeg";
  mime_types[".gif"] = "image/gif";
  mime_types[".svg"] = "image/svg+xml";
  mime_types[".json"] = "application/json";
}

std::string get_mime_type(const std::string& path) {
  size_t dot_pos = path.find_last_of(".");
  if (dot_pos != std::string::npos) {
    std::string extension = path.substr(dot_pos);
    if (mime_types.find(extension) != mime_types.end()) {
      return mime_types[extension];
    }
  }
  return "application/octet-stream";
}

std::string get_file_content(const std::string& path) {
#ifdef _WIN32
  std::string full_path = "../../../../CitySprintHTTPServer/templates/" + path;
#else
  std::string full_path = "../../CitySprintHTTPServer/templates/" + path;
#endif

  std::ifstream file(full_path, std::ios::binary);
  if (!file) {
    std::cout << "File not found: " << full_path << std::endl;
    return "";
  }

  return std::string((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
}

void handle_client(SOCKET client_socket) {
  char buffer[4096];
  int read_bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
  if (read_bytes < 0) {
    std::cerr << "recv failed" << std::endl;
    closesocket(client_socket);
    return;
  }
  buffer[read_bytes] = '\0';

  std::istringstream request_stream(buffer);
  std::string method, path, version;
  request_stream >> method >> path >> version;

  std::cout << "[" << get_current_time() << "] " << method << " for " << path << std::endl;

  if (method == "GET") {
    std::string file_path = path;
    if (path == "/") {
      file_path = "citysprint.html";
    }

    auto route = get_routes.find(file_path);
    if (route != get_routes.end()) {
      std::cout << "Found route handler for: " << file_path << std::endl;
      std::string response = route->second(file_path);
      send(client_socket, response.c_str(), response.size(), 0);
      closesocket(client_socket);
      return;
    }

    std::string file_content = get_file_content(file_path);
    if (file_content.empty()) {
      std::cout << "File content is empty for: " << file_path << std::endl;
      std::string response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot Found";
      send(client_socket, response.c_str(), response.size(), 0);
      closesocket(client_socket);
      return;
    }

    std::string mime_type = get_mime_type(file_path);
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: " << mime_type << "\r\n";
    response << "Content-Length: " << file_content.size() << "\r\n";
    response << "\r\n";
    response << file_content;
    send(client_socket, response.str().c_str(), response.str().size(), 0);
  } else {
    std::string response = "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\n\r\nMethod not allowed";
    send(client_socket, response.c_str(), response.size(), 0);
  }

  closesocket(client_socket);
}

void initialize_routes() {
  // Pretty simple map structure that stores each of the existing routes for the web pages
  get_routes["/"] = [](const std::string&) { return handle_get_request("/citysprint.html"); };
  get_routes["/new"] = [](const std::string&) { return handle_get_request("/newBoard.html"); };

  // Define the two sets of POST routes that we have so far
  post_routes["/api/echo"] = [](const std::string& body) {
    std::cout << body << std::endl;
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"echo\": \"" + body + "\"}";
  };
  
  post_routes["/api/reverse"] = [](const std::string& body) {
    std::string reversed(body.rbegin(), body.rend());
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"reversed\": \"" + reversed + "\"}";
  };
} 

std::string handle_get_request(const std::string& path) {
  std::cout << "Handling GET request for path: " << path << std::endl;

  std::cout << "Requested path: " << path << std::endl;

  std::string file_path = path;
  if (path == "/") {
    file_path = "citysprint.html";
  }

  auto route = get_routes.find(file_path);
  if (route != get_routes.end()) {
    std::cout << "Found route handler for: " << file_path << std::endl;
    return route->second(file_path);
  }

  std::string file_content = get_file_content(file_path);
  if (file_content.empty()) {
    std::cout << "File content is empty for: " << file_path << std::endl;
    return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot Found";
  }

  std::string mime_type = get_mime_type(file_path);
  std::ostringstream response;
  response << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: " << mime_type << "\r\n"
           << "Content-Length: " << file_content.size() << "\r\n"
           << "\r\n"
           << file_content;

  std::cout << "Serving file: " << file_path << " with MIME type: " << mime_type << std::endl;
  return response.str();
}

std::string handle_post_request(const std::string& path, const std::string& body) {
  auto route = post_routes.find(path);
  if (route != post_routes.end()) {
    return route->second(body);
  }
  return "HTTP/1.1 404 Not Found\r\n\r\nEndpoint not found";
}

std::string get_current_time() {
  time_t now = time(0);
  struct tm *tstruct = localtime(&now);
  char buf[80];
  strftime(buf, sizeof(buf), "%Y-%m-%d %X", tstruct);
  return buf;
}
