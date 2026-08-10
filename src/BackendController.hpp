#pragma once

#include <MCDevLink/Event.hpp>
#include <MCDevLink/Protocol/Safaia.hpp>
#include <MCDevLink/Runtime.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace mcdev {

enum class LogFilter {
    all,
    warning,
    error,
};

struct LogLine {
    MCDevLink::SessionId sessionId = 0;
    MCDevLink::LogLevel level = MCDevLink::LogLevel::unknown;
    std::string message;
};

struct SessionSummary {
    MCDevLink::SessionId id = 0;
    MCDevLink::SessionState state = MCDevLink::SessionState::disconnected;
    MCDevLink::Endpoint remote;
    std::string clientName;
    std::string connectId;
    std::size_t logCount = 0;
};

class BackendController {
public:
    BackendController();
    ~BackendController();

    BackendController(const BackendController&) = delete;
    BackendController& operator=(const BackendController&) = delete;

    bool start(std::function<void()> requestUiUpdate = {});
    void drainPendingEvents();
    void clearLogs();

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] const std::string& startError() const noexcept;
    [[nodiscard]] const std::string& lastDiagnostic() const noexcept;
    [[nodiscard]] const MCDevLink::Endpoint& localEndpoint() const noexcept;
    [[nodiscard]] const std::vector<SessionSummary>& sessions() const noexcept;
    [[nodiscard]] const std::vector<LogLine>& logs() const noexcept;
    [[nodiscard]] std::size_t revision() const noexcept;
    [[nodiscard]] std::size_t readySessionCount() const noexcept;
    [[nodiscard]] std::size_t maximumMessageBytes() const noexcept;

    [[nodiscard]] const SessionSummary* findSession(MCDevLink::SessionId id) const noexcept;
    [[nodiscard]] const std::vector<std::size_t>& filteredLogIndices(
        std::string_view query,
        LogFilter filter,
        std::optional<MCDevLink::SessionId> session);

private:
    using PendingEvent = std::variant<
        MCDevLink::LogEvent,
        MCDevLink::SessionEvent,
        MCDevLink::DiagnosticEvent>;

    struct PartialLine {
        std::string residual;
        MCDevLink::LogLevel level = MCDevLink::LogLevel::unknown;
    };

    struct FilterCache {
        std::size_t revision = static_cast<std::size_t>(-1);
        std::string query;
        LogFilter filter = LogFilter::all;
        std::optional<MCDevLink::SessionId> session;
        std::vector<std::size_t> indices;
    };

    void consumeLog(const MCDevLink::LogEvent& event);
    void consumeSession(const MCDevLink::SessionEvent& event);
    void consumeDiagnostic(const MCDevLink::DiagnosticEvent& event);
    void enqueueEvent(PendingEvent event);
    void runPollLoop(std::stop_token stopToken);
    void flushPartial(MCDevLink::SessionId sessionId);
    void appendLine(MCDevLink::SessionId sessionId, PartialLine& partial, std::string line);
    void trimLogsIfNeeded();

    MCDevLink::Runtime runtime_;
    MCDevLink::Protocol::SafaiaService service_;
    bool startAttempted_ = false;
    std::atomic_bool running_{false};
    std::string startError_;
    std::string lastDiagnostic_;
    MCDevLink::Endpoint localEndpoint_;
    std::vector<SessionSummary> sessions_;
    std::vector<LogLine> logs_;
    std::unordered_map<MCDevLink::SessionId, PartialLine> partialLines_;
    std::size_t maximumMessageBytes_ = 0;
    std::size_t revision_ = 0;
    FilterCache filterCache_;
    std::function<void()> requestUiUpdate_;
    std::mutex pendingEventsMutex_;
    std::vector<PendingEvent> pendingEvents_;
    std::jthread pollThread_;
};

} // namespace mcdev
