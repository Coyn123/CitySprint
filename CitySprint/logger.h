#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>

class Logger {
public:
  static Logger& getInstance();
  void log(const std::string& message);
  void close();

private:
  Logger();
  ~Logger();
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  std::ofstream logFile;
  std::mutex logMutex;
};

#endif // LOGGER_H
