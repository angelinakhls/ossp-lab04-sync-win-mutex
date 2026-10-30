#pragma once

#include <string>
#include <vector>
#include <mutex>

class Logger {
public:
    static Logger& Instance();
    void Log(const std::string& message);
    std::vector<std::string> GetEvents() const;

private:
    Logger() = default;
    mutable std::mutex mutex_;
    std::vector<std::string> events_;
};
