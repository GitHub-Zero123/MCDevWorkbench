#include "BackendController.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <utility>

namespace mcdev {
namespace {

constexpr std::size_t kMaximumLogLines = 50000;
constexpr std::size_t kLogTrimBatch = 5000;
constexpr std::size_t kMaximumRetainedSessions = 256;

bool containsIgnoreCase(std::string_view value, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    return std::search(
               value.begin(), value.end(), needle.begin(), needle.end(),
               [](const char left, const char right) {
                   return std::tolower(static_cast<unsigned char>(left))
                       == std::tolower(static_cast<unsigned char>(right));
               }) != value.end();
}

MCDevLink::LogLevel inferLevel(const MCDevLink::LogLevel level, std::string_view line) {
    if (level != MCDevLink::LogLevel::unknown) {
        return level;
    }
    if (containsIgnoreCase(line, "fatal") || containsIgnoreCase(line, "error")
        || containsIgnoreCase(line, "exception") || containsIgnoreCase(line, "traceback")) {
        return MCDevLink::LogLevel::error;
    }
    if (containsIgnoreCase(line, "warn")) {
        return MCDevLink::LogLevel::warning;
    }
    if (containsIgnoreCase(line, "debug")) {
        return MCDevLink::LogLevel::debug;
    }
    if (containsIgnoreCase(line, "trace")) {
        return MCDevLink::LogLevel::trace;
    }
    if (containsIgnoreCase(line, "info")) {
        return MCDevLink::LogLevel::info;
    }
    return MCDevLink::LogLevel::unknown;
}

std::string formatTimestamp(const std::chrono::system_clock::time_point time) {
    const auto resolved = time == std::chrono::system_clock::time_point{}
        ? std::chrono::system_clock::now()
        : time;
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(resolved);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(resolved - seconds);
    const std::time_t rawTime = std::chrono::system_clock::to_time_t(resolved);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &rawTime);
#else
    localtime_r(&rawTime, &local);
#endif
    char buffer[20] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%02d:%02d:%02d.%03d",
        local.tm_hour,
        local.tm_min,
        local.tm_sec,
        static_cast<int>(milliseconds.count()));
    return buffer;
}

std::string normalizeLine(std::string line) {
    std::string normalized;
    normalized.reserve(line.size());
    for (const unsigned char character : line) {
        if (character == '\t') {
            normalized.append("    ");
        } else if (character >= 0x20U || character >= 0x80U) {
            normalized.push_back(static_cast<char>(character));
        } else {
            normalized.push_back('?');
        }
    }
    return normalized;
}

bool matchesFilter(const MCDevLink::LogLevel level, const LogFilter filter) {
    switch (filter) {
        case LogFilter::all:
            return true;
        case LogFilter::warning:
            return level == MCDevLink::LogLevel::warning;
        case LogFilter::error:
            return level == MCDevLink::LogLevel::error
                || level == MCDevLink::LogLevel::critical;
    }
    return true;
}

} // namespace

BackendController::BackendController()
    : service_(runtime_) {
    service_.setLogHandler([this](const MCDevLink::LogEvent& event) { consumeLog(event); });
    service_.setSessionHandler([this](const MCDevLink::SessionEvent& event) {
        consumeSession(event);
    });
    service_.setDiagnosticHandler([this](const MCDevLink::DiagnosticEvent& event) {
        consumeDiagnostic(event);
    });
}

BackendController::~BackendController() {
    service_.stop();
}

bool BackendController::start() {
    if (startAttempted_) {
        return service_.isRunning();
    }
    startAttempted_ = true;
    if (const std::error_code error = service_.start()) {
        startError_ = error.category().name() + std::string(": ") + error.message();
        ++revision_;
        return false;
    }
    localEndpoint_ = service_.localEndpoint();
    ++revision_;
    return true;
}

void BackendController::poll() {
    if (!service_.isRunning()) {
        return;
    }
    (void)runtime_.poll({256, std::chrono::microseconds{1000}});
}

void BackendController::clearLogs() {
    logs_.clear();
    partialLines_.clear();
    for (auto& session : sessions_) {
        session.logCount = 0;
    }
    ++revision_;
}

bool BackendController::isRunning() const noexcept {
    return service_.isRunning();
}

const std::string& BackendController::startError() const noexcept {
    return startError_;
}

const std::string& BackendController::lastDiagnostic() const noexcept {
    return lastDiagnostic_;
}

const MCDevLink::Endpoint& BackendController::localEndpoint() const noexcept {
    return localEndpoint_;
}

const std::vector<SessionSummary>& BackendController::sessions() const noexcept {
    return sessions_;
}

const std::vector<LogLine>& BackendController::logs() const noexcept {
    return logs_;
}

std::size_t BackendController::revision() const noexcept {
    return revision_;
}

std::size_t BackendController::readySessionCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        sessions_.begin(), sessions_.end(), [](const SessionSummary& session) {
            return session.state == MCDevLink::SessionState::ready;
        }));
}

const SessionSummary* BackendController::findSession(const MCDevLink::SessionId id) const noexcept {
    const auto found = std::find_if(
        sessions_.begin(), sessions_.end(), [id](const SessionSummary& session) {
            return session.id == id;
        });
    return found == sessions_.end() ? nullptr : &*found;
}

const std::vector<std::size_t>& BackendController::filteredLogIndices(
    const std::string_view query,
    const LogFilter filter,
    const std::optional<MCDevLink::SessionId> session) {
    if (filterCache_.revision == revision_ && filterCache_.query == query
        && filterCache_.filter == filter && filterCache_.session == session) {
        return filterCache_.indices;
    }

    filterCache_.revision = revision_;
    filterCache_.query.assign(query);
    filterCache_.filter = filter;
    filterCache_.session = session;
    filterCache_.indices.clear();
    filterCache_.indices.reserve(logs_.size());
    for (std::size_t index = 0; index < logs_.size(); ++index) {
        const LogLine& line = logs_[index];
        if (session.has_value() && line.sessionId != *session) {
            continue;
        }
        if (!matchesFilter(line.level, filter)) {
            continue;
        }
        if (!containsIgnoreCase(line.message, query) && !containsIgnoreCase(line.source, query)) {
            continue;
        }
        filterCache_.indices.push_back(index);
    }
    return filterCache_.indices;
}

void BackendController::consumeLog(const MCDevLink::LogEvent& event) {
    PartialLine& partial = partialLines_[event.sessionId];
    partial.source = event.source;
    partial.level = event.level;
    partial.time = event.time;
    partial.residual += event.message;

    std::size_t newline = 0;
    while ((newline = partial.residual.find('\n')) != std::string::npos) {
        std::string line = partial.residual.substr(0, newline);
        partial.residual.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        appendLine(event.sessionId, partial, std::move(line));
    }
}

void BackendController::consumeSession(const MCDevLink::SessionEvent& event) {
    auto found = std::find_if(
        sessions_.begin(), sessions_.end(), [&event](const SessionSummary& session) {
            return session.id == event.sessionId;
        });
    if (found == sessions_.end()) {
        sessions_.push_back({
            event.sessionId,
            event.state,
            event.remote,
            event.clientName,
            event.connectId,
            0,
        });
    } else {
        found->state = event.state;
        found->remote = event.remote;
        found->clientName = event.clientName;
        found->connectId = event.connectId;
    }

    if (event.state == MCDevLink::SessionState::disconnected) {
        flushPartial(event.sessionId);
    }
    while (sessions_.size() > kMaximumRetainedSessions) {
        const auto disconnected = std::find_if(
            sessions_.begin(), sessions_.end(), [](const SessionSummary& session) {
                return session.state == MCDevLink::SessionState::disconnected;
            });
        if (disconnected == sessions_.end()) {
            break;
        }
        sessions_.erase(disconnected);
    }
    ++revision_;
}

void BackendController::consumeDiagnostic(const MCDevLink::DiagnosticEvent& event) {
    const char* prefix = "Info";
    if (event.level == MCDevLink::DiagnosticLevel::warning) {
        prefix = "Warning";
    } else if (event.level == MCDevLink::DiagnosticLevel::error) {
        prefix = "Error";
    }
    lastDiagnostic_ = std::string(prefix) + ": " + event.message;
    ++revision_;
}

void BackendController::flushPartial(const MCDevLink::SessionId sessionId) {
    const auto found = partialLines_.find(sessionId);
    if (found == partialLines_.end()) {
        return;
    }
    if (!found->second.residual.empty()) {
        std::string line = std::move(found->second.residual);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        appendLine(sessionId, found->second, std::move(line));
    }
    partialLines_.erase(found);
}

void BackendController::appendLine(
    const MCDevLink::SessionId sessionId,
    PartialLine& partial,
    std::string line) {
    trimLogsIfNeeded();
    const MCDevLink::LogLevel level = inferLevel(partial.level, line);
    logs_.push_back({
        sessionId,
        level,
        formatTimestamp(partial.time),
        partial.source.empty() ? "Safaia" : partial.source,
        normalizeLine(std::move(line)),
    });
    if (auto* session = const_cast<SessionSummary*>(findSession(sessionId))) {
        ++session->logCount;
    }
    ++revision_;
}

void BackendController::trimLogsIfNeeded() {
    if (logs_.size() < kMaximumLogLines) {
        return;
    }
    const std::size_t eraseCount = std::min(kLogTrimBatch, logs_.size());
    logs_.erase(logs_.begin(), logs_.begin() + static_cast<std::ptrdiff_t>(eraseCount));
}

} // namespace mcdev
