#include "logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::ostringstream oss;
    oss << "[" << std::setw(6) << ms << " ms] " << message;
    events_.push_back(oss.str());
}

std::vector<std::string> Logger::GetEvents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
}
