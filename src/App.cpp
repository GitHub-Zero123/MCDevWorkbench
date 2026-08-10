#include "BackendController.hpp"
#include "platform/WindowTheme.hpp"

#include "eui_neo.h"
#include "core/window/window_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace app {

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = [] {
        const mcdev::platform::WindowSize size =
            mcdev::platform::initialWindowSize(910, 572);
        DslAppConfig result = DslAppConfig{}
            .title("MCDev Workbench")
            .pageId("mcdev_workbench")
            .clearColor({0.129f, 0.129f, 0.129f, 1.0f})
            .windowSize(size.width, size.height)
            .showDebugStatsInTitle(false)
            .fps(60.0)
            .iconPath("");
        if (const std::string font = mcdev::platform::preferredUiFont(); !font.empty()) {
            result.textFont(font);
        }
        return result;
    }();
    return config;
}

namespace {

constexpr float kHeaderHeight = 86.0f;
constexpr float kToolbarHeight = 70.0f;
constexpr float kColumnHeaderHeight = 38.0f;
constexpr float kFooterHeight = 36.0f;
constexpr float kLogRowHeight = 40.0f;
constexpr float kHorizontalScrollHeight = 16.0f;
constexpr float kLogLeft = 22.0f;
constexpr float kLevelColumnWidth = 60.0f;
constexpr float kEstimatedLogByteWidth = 8.25f;
constexpr float kLogSliceOverscan = 256.0f;
constexpr std::size_t kMaximumRenderedLogBytes = 4096;
constexpr float kSidebarMinimumWidth = 188.0f;
constexpr float kSidebarMaximumWidth = 320.0f;
constexpr float kMainMinimumWidth = 640.0f;
constexpr float kSplitterHitWidth = 8.0f;

const eui::Color kCanvas{0.129f, 0.129f, 0.129f, 1.0f};
const eui::Color kSidebar{0.090f, 0.090f, 0.090f, 1.0f};
const eui::Color kBand{0.122f, 0.122f, 0.122f, 1.0f};
const eui::Color kRaised{0.184f, 0.184f, 0.184f, 1.0f};
const eui::Color kHover{0.220f, 0.220f, 0.220f, 1.0f};
const eui::Color kPressed{0.267f, 0.267f, 0.267f, 1.0f};
const eui::Color kBorder{0.239f, 0.239f, 0.239f, 1.0f};
const eui::Color kText{0.925f, 0.925f, 0.925f, 1.0f};
const eui::Color kMuted{0.706f, 0.706f, 0.706f, 1.0f};
const eui::Color kFaint{0.557f, 0.557f, 0.557f, 1.0f};
const eui::Color kGreen{0.063f, 0.64f, 0.50f, 1.0f};
const eui::Color kStatusGreen{0.180f, 0.820f, 0.600f, 1.0f};
const eui::Color kDebug{0.42f, 0.72f, 0.78f, 1.0f};
const eui::Color kWarning{0.86f, 0.66f, 0.24f, 1.0f};
const eui::Color kError{0.90f, 0.36f, 0.42f, 1.0f};
const eui::Color kCritical{1.0f, 0.32f, 0.40f, 1.0f};

struct WorkbenchState {
    mcdev::BackendController backend;
    eui::Signal<std::string> search{""};
    eui::Signal<int> filter{0};
    eui::Signal<bool> follow{true};
    eui::Signal<float> logScroll{0.0f};
    eui::Signal<float> horizontalScroll{0.0f};
    eui::Signal<float> sessionScroll{0.0f};
    float horizontalDragStart = 0.0f;
    float horizontalDragTravelPixels = 0.0f;
    float verticalDragStart = 0.0f;
    float verticalDragTravelPixels = 0.0f;
    float sidebarWidth = 0.0f;
    float sidebarDragStart = 0.0f;
    float sidebarDragLogicalPerPixel = 1.0f;
    std::optional<std::size_t> selectedLogIndex;
    std::string selectedLogMessage;
    std::optional<MCDevLink::SessionId> selectedSession;
};

WorkbenchState& state() {
    static WorkbenchState value;
    return value;
}

components::theme::ThemeColorTokens theme() {
    auto tokens = components::theme::dark();
    tokens.background = kCanvas;
    tokens.primary = kText;
    tokens.surface = kRaised;
    tokens.surfaceHover = kHover;
    tokens.surfaceActive = kPressed;
    tokens.text = kText;
    tokens.border = kBorder;
    tokens.metrics.radius.small = 5.0f;
    tokens.metrics.radius.control = 6.0f;
    tokens.metrics.radius.popup = 7.0f;
    tokens.metrics.radius.overlay = 6.0f;
    tokens.metrics.radius.section = 6.0f;
    return tokens;
}

eui::Transition transition() {
    return eui::Transition::make(0.14f, eui::Ease::OutCubic);
}

std::string endpointText(const MCDevLink::Endpoint& endpoint) {
    if (endpoint.address.empty()) {
        return "Not bound";
    }
    return endpoint.address + ":" + std::to_string(endpoint.port);
}

std::string sessionTitle(const mcdev::SessionSummary& session) {
    return session.clientName.empty()
        ? "Session " + std::to_string(session.id)
        : session.clientName;
}

std::string sessionStateText(const MCDevLink::SessionState value) {
    switch (value) {
        case MCDevLink::SessionState::connected:
            return "CONNECTED";
        case MCDevLink::SessionState::ready:
            return "READY";
        case MCDevLink::SessionState::disconnected:
            return "OFFLINE";
    }
    return "UNKNOWN";
}

eui::Color sessionStateColor(const MCDevLink::SessionState value) {
    switch (value) {
        case MCDevLink::SessionState::connected:
        case MCDevLink::SessionState::ready:
            return kGreen;
        case MCDevLink::SessionState::disconnected:
            return kFaint;
    }
    return kFaint;
}

std::string levelText(const MCDevLink::LogLevel value) {
    switch (value) {
        case MCDevLink::LogLevel::trace:
            return "TRACE";
        case MCDevLink::LogLevel::debug:
            return "DEBUG";
        case MCDevLink::LogLevel::info:
            return "INFO";
        case MCDevLink::LogLevel::warning:
            return "WARN";
        case MCDevLink::LogLevel::error:
            return "ERROR";
        case MCDevLink::LogLevel::critical:
            return "FATAL";
        case MCDevLink::LogLevel::unknown:
            return "LOG";
    }
    return "LOG";
}

eui::Color withAlpha(const eui::Color color, const float alpha) {
    return {color.r, color.g, color.b, alpha};
}

struct LogVisualStyle {
    eui::Color background;
    eui::Color level;
    eui::Color message;
    int levelWeight = 650;
    int messageWeight = 450;
};

LogVisualStyle logVisualStyle(const mcdev::LogLine& line, const bool alternateRow) {
    LogVisualStyle style{
        alternateRow ? kBand : kCanvas,
        kMuted,
        kText,
    };

    if (line.message.find("[INFO][Developer]") != std::string::npos) {
        style.level = withAlpha(kFaint, 0.52f);
        style.message = withAlpha(kFaint, 0.48f);
        style.levelWeight = 500;
        style.messageWeight = 400;
        return style;
    }
    if (line.message.find("SUC") != std::string::npos) {
        style.background = {0.118f, 0.165f, 0.145f, 1.0f};
        style.level = withAlpha(kGreen, 0.92f);
        style.message = withAlpha(kText, 0.92f);
        return style;
    }

    switch (line.level) {
        case MCDevLink::LogLevel::trace:
            style.level = withAlpha(kFaint, 0.52f);
            style.message = withAlpha(kFaint, 0.52f);
            style.levelWeight = 500;
            style.messageWeight = 400;
            break;
        case MCDevLink::LogLevel::debug:
            style.background = alternateRow
                ? eui::Color{0.122f, 0.145f, 0.149f, 1.0f}
                : eui::Color{0.129f, 0.151f, 0.157f, 1.0f};
            style.level = withAlpha(kDebug, 0.82f);
            style.message = withAlpha(kMuted, 0.76f);
            style.levelWeight = 600;
            break;
        case MCDevLink::LogLevel::info:
            style.level = withAlpha(kMuted, 0.88f);
            style.message = withAlpha(kText, 0.94f);
            break;
        case MCDevLink::LogLevel::warning:
            style.background = {0.173f, 0.157f, 0.118f, 1.0f};
            style.level = kWarning;
            break;
        case MCDevLink::LogLevel::error:
            style.background = {0.176f, 0.122f, 0.133f, 1.0f};
            style.level = kError;
            style.levelWeight = 700;
            break;
        case MCDevLink::LogLevel::critical:
            style.background = {0.204f, 0.106f, 0.122f, 1.0f};
            style.level = kCritical;
            style.message = kText;
            style.levelWeight = 750;
            style.messageWeight = 550;
            break;
        case MCDevLink::LogLevel::unknown:
            style.level = withAlpha(kFaint, 0.62f);
            style.message = withAlpha(kMuted, 0.62f);
            style.levelWeight = 500;
            style.messageWeight = 400;
            break;
    }
    return style;
}

mcdev::LogFilter selectedFilter(const int value) {
    if (value == 1) {
        return mcdev::LogFilter::warning;
    }
    if (value == 2) {
        return mcdev::LogFilter::error;
    }
    return mcdev::LogFilter::all;
}

void clippedText(
    eui::Ui& ui,
    const std::string& id,
    const std::string& value,
    float x,
    float y,
    float width,
    float height,
    float fontSize,
    const eui::Color color,
    const int weight = 400,
    const char* fontFamily = nullptr) {
    ui.stack(id + ".clip")
        .position(x, y)
        .size(std::max(0.0f, width), std::max(0.0f, height))
        .clip()
        .content([&] {
            auto text = ui.text(id)
                .size(std::max(0.0f, width), std::max(0.0f, height))
                .text(value)
                .fontSize(fontSize)
                .fontWeight(weight)
                .lineHeight(fontSize + 2.0f)
                .color(color)
                .verticalAlign(eui::VerticalAlign::Center)
                .wrap(false);
            if (fontFamily != nullptr) {
                text.fontFamily(fontFamily);
            }
            text.build();
        })
        .build();
}

void composeTerminalGlyph(
    eui::Ui& ui,
    const std::string& id,
    const float x,
    const float y,
    const float size,
    const eui::Color color) {
    const float scale = size / 38.0f;
    const auto block = [&](const char* suffix, float left, float top, float width, float height) {
        ui.rect(id + suffix)
            .position(x + left * scale, y + top * scale)
            .size(width * scale, height * scale)
            .radius(0.8f * scale)
            .color(color)
            .build();
    };

    block(".chevron.0", 10.0f, 9.0f, 4.0f, 4.0f);
    block(".chevron.1", 13.0f, 12.0f, 4.0f, 4.0f);
    block(".chevron.2", 16.0f, 15.0f, 4.0f, 4.0f);
    block(".chevron.3", 16.0f, 18.0f, 4.0f, 4.0f);
    block(".chevron.4", 13.0f, 21.0f, 4.0f, 4.0f);
    block(".chevron.5", 10.0f, 24.0f, 4.0f, 4.0f);
    block(".underscore", 21.0f, 24.0f, 9.0f, 4.0f);
}

void composeClearGlyph(
    eui::Ui& ui,
    const std::string& id,
    const float x,
    const float y,
    const eui::Color color) {
    const auto stroke = [&](const char* suffix, float left, float top, float width, float height) {
        ui.rect(id + suffix)
            .position(x + left, y + top)
            .size(width, height)
            .radius(1.0f)
            .color(color)
            .build();
    };

    stroke(".handle", 18.0f, 9.0f, 8.0f, 2.0f);
    stroke(".lid", 14.0f, 12.0f, 16.0f, 2.0f);
    stroke(".left", 16.0f, 16.0f, 2.0f, 13.0f);
    stroke(".right", 26.0f, 16.0f, 2.0f, 13.0f);
    stroke(".bottom", 16.0f, 28.0f, 12.0f, 2.0f);
    stroke(".slot.0", 20.0f, 17.0f, 1.5f, 9.0f);
    stroke(".slot.1", 23.0f, 17.0f, 1.5f, 9.0f);
}

void composeSessionRow(
    eui::Ui& ui,
    const std::string& rowId,
    const std::int64_t index,
    const float width,
    const float height) {
    WorkbenchState& workbench = state();
    const bool allSessions = index == 0;
    const auto& sessions = workbench.backend.sessions();
    const mcdev::SessionSummary* session = allSessions
        ? nullptr
        : &sessions[static_cast<std::size_t>(index - 1)];
    const bool selected = allSessions
        ? !workbench.selectedSession.has_value()
        : workbench.selectedSession == session->id;

    const eui::Color normal = selected ? kRaised : eui::Color{0.0f, 0.0f, 0.0f, 0.0f};
    ui.rect(rowId + ".hit")
        .position(10.0f, 5.0f)
        .size(std::max(0.0f, width - 20.0f), height - 10.0f)
        .states(normal, kHover, kPressed)
        .radius(6.0f)
        .transition(transition())
        .onClick([allSessions, sessionId = session != nullptr ? session->id : 0] {
            state().selectedSession = allSessions
                ? std::nullopt
                : std::optional<MCDevLink::SessionId>{sessionId};
            state().logScroll.set(0.0f);
        })
        .build();

    if (selected) {
        ui.rect(rowId + ".selected")
            .position(10.0f, 14.0f)
            .size(2.0f, height - 28.0f)
            .radius(1.0f)
            .color(kText)
            .build();
    }

    if (allSessions) {
        clippedText(
            ui,
            rowId + ".title",
            "All sessions",
            24.0f,
            5.0f,
            width - 80.0f,
            height - 10.0f,
            15.0f,
            selected ? kText : kMuted,
            selected ? 650 : 500);
        clippedText(
            ui,
            rowId + ".count",
            std::to_string(workbench.backend.logs().size()),
            width - 52.0f,
            5.0f,
            28.0f,
            height - 10.0f,
            13.0f,
            kFaint);
        return;
    }

    ui.rect(rowId + ".status")
        .position(28.0f, 21.0f)
        .size(7.0f, 7.0f)
        .radius(4.0f)
        .color(sessionStateColor(session->state))
        .build();
    clippedText(
        ui,
        rowId + ".title",
        sessionTitle(*session),
        48.0f,
        8.0f,
        width - 76.0f,
        24.0f,
        15.0f,
        selected ? kText : kMuted,
        selected ? 650 : 500);
    clippedText(
        ui,
        rowId + ".subtitle",
        sessionStateText(session->state) + "  " + endpointText(session->remote),
        48.0f,
        32.0f,
        width - 76.0f,
        20.0f,
        11.5f,
        kFaint);
}

void composeSidebar(eui::Ui& ui, const float width, const float height) {
    WorkbenchState& workbench = state();
    const float listY = 112.0f;
    const float footerHeight = 96.0f;
    const float listHeight = std::max(80.0f, height - listY - footerHeight);

    ui.stack("sidebar")
        .size(width, height)
        .clip()
        .content([&] {
            ui.rect("sidebar.background").size(width, height).color(kSidebar).build();
            ui.rect("sidebar.border")
                .position(width - 1.0f, 0.0f)
                .size(1.0f, height)
                .color(kBorder)
                .build();

            ui.rect("brand.mark")
                .position(18.0f, 18.0f)
                .size(38.0f, 38.0f)
                .radius(6.0f)
                .color(kText)
                .build();
            composeTerminalGlyph(ui, "brand.mark.glyph", 18.0f, 18.0f, 38.0f, kCanvas);
            clippedText(
                ui,
                "brand.title",
                "MCDev Workbench",
                68.0f,
                14.0f,
                width - 82.0f,
                28.0f,
                16.5f,
                kText,
                720);
            clippedText(
                ui,
                "brand.subtitle",
                "LOG CONSOLE",
                68.0f,
                42.0f,
                width - 82.0f,
                20.0f,
                10.5f,
                kFaint,
                650);

            clippedText(
                ui,
                "sessions.label",
                "SESSIONS",
                20.0f,
                86.0f,
                width - 40.0f,
                20.0f,
                11.5f,
                kFaint,
                650);

            components::virtualList(ui, "sessions.list")
                .position(0.0f, listY)
                .size(width, listHeight)
                .itemCount(static_cast<std::int64_t>(workbench.backend.sessions().size() + 1))
                .rowHeight(60.0f)
                .bind(workbench.sessionScroll)
                .step(60.0f)
                .overscanViewports(0.5f)
                .scrollbarWidth(4.0f)
                .scrollbarGap(3.0f)
                .theme(theme())
                .transition(transition())
                .row(composeSessionRow)
                .build();

            const float footerY = height - footerHeight;
            ui.rect("sidebar.footer.border")
                .position(0.0f, footerY)
                .size(width, 1.0f)
                .color(kBorder)
                .build();
            const bool running = workbench.backend.isRunning();
            ui.rect("backend.status.dot")
                .position(20.0f, footerY + 24.0f)
                .size(8.0f, 8.0f)
                .radius(4.0f)
                .color(running ? kGreen : kError)
                .build();
            clippedText(
                ui,
                "backend.status",
                running ? "Safaia receiver" : "Receiver unavailable",
                40.0f,
                footerY + 18.0f,
                width - 58.0f,
                26.0f,
                14.0f,
                kMuted,
                600);
            clippedText(
                ui,
                "backend.endpoint",
                running ? endpointText(workbench.backend.localEndpoint())
                        : workbench.backend.startError(),
                20.0f,
                footerY + 52.0f,
                width - 38.0f,
                23.0f,
                12.5f,
                kFaint);
        })
        .build();
}

void composeLogCell(
    eui::Ui& ui,
    const std::string& id,
    const std::string& value,
    const float x,
    const float width,
    const eui::Color color,
    const float fontSize = 13.5f,
    const int weight = 450) {
    clippedText(
        ui,
        id,
        value,
        x,
        0.0f,
        width,
        kLogRowHeight,
        fontSize,
        color,
        weight,
        "monospace");
}

struct RenderedLogSlice {
    std::string text;
    float x = 0.0f;
    float width = 0.0f;
};

bool isUtf8ContinuationByte(const char value) {
    return (static_cast<unsigned char>(value) & 0xc0u) == 0x80u;
}

RenderedLogSlice sliceLogMessage(
    const std::string& message,
    const float horizontalOffset,
    const float viewportWidth) {
    const float firstVisiblePixel = std::max(0.0f, horizontalOffset - kLogSliceOverscan);
    std::size_t begin = std::min(
        message.size(),
        static_cast<std::size_t>(firstVisiblePixel / kEstimatedLogByteWidth));
    while (begin < message.size() && isUtf8ContinuationByte(message[begin])) {
        ++begin;
    }
    if (begin >= message.size()) {
        return {{}, 0.0f, viewportWidth};
    }

    const std::size_t viewportBytes = static_cast<std::size_t>(
        (std::max(0.0f, viewportWidth) + 2.0f * kLogSliceOverscan)
        / kEstimatedLogByteWidth) + 4;
    const std::size_t renderedBytes = std::min(kMaximumRenderedLogBytes, viewportBytes);
    std::size_t end = std::min(message.size(), begin + renderedBytes);
    while (end > begin && end < message.size() && isUtf8ContinuationByte(message[end])) {
        --end;
    }

    const float x = static_cast<float>(begin) * kEstimatedLogByteWidth - horizontalOffset;
    return {
        message.substr(begin, end - begin),
        x,
        std::max(0.0f, viewportWidth - x + kLogSliceOverscan),
    };
}

void composeLogRow(
    eui::Ui& ui,
    const std::string& rowId,
    const std::int64_t visibleIndex,
    const float width,
    const float height) {
    WorkbenchState& workbench = state();
    const auto& indices = workbench.backend.filteredLogIndices(
        workbench.search.get(),
        selectedFilter(workbench.filter.get()),
        workbench.selectedSession);
    if (visibleIndex < 0 || static_cast<std::size_t>(visibleIndex) >= indices.size()) {
        return;
    }
    const mcdev::LogLine& line = workbench.backend.logs()[indices[static_cast<std::size_t>(visibleIndex)]];
    const float messageX = kLogLeft + kLevelColumnWidth;
    const float messageViewportWidth = std::max(0.0f, width - messageX - 18.0f);
    const float messageContentWidth = std::max(
        messageViewportWidth,
        std::min(
            50000.0f,
            static_cast<float>(workbench.backend.maximumMessageBytes()) * 8.25f + 20.0f));
    const float horizontalOffset = std::clamp(
        workbench.horizontalScroll.get(),
        0.0f,
        std::max(0.0f, messageContentWidth - messageViewportWidth));

    const LogVisualStyle visual = logVisualStyle(line, visibleIndex % 2 != 0);
    const RenderedLogSlice messageSlice =
        sliceLogMessage(line.message, horizontalOffset, messageViewportWidth);
    const std::size_t logIndex = indices[static_cast<std::size_t>(visibleIndex)];
    const bool selected = workbench.selectedLogIndex == logIndex
        && workbench.selectedLogMessage == line.message;
    ui.rect(rowId + ".background")
        .size(width, height)
        .color(selected ? withAlpha(kText, 0.075f) : visual.background)
        .build();
    ui.rect(rowId + ".border")
        .position(0.0f, height - 1.0f)
        .size(width, 1.0f)
        .color({kBorder.r, kBorder.g, kBorder.b, 0.55f})
        .build();

    composeLogCell(
        ui,
        rowId + ".level",
        levelText(line.level),
        kLogLeft,
        kLevelColumnWidth - 8.0f,
        visual.level,
        12.0f,
        visual.levelWeight);
    ui.stack(rowId + ".message.viewport")
        .position(messageX, 0.0f)
        .size(messageViewportWidth, kLogRowHeight)
        .clip()
        .content([&] {
            clippedText(
                ui,
                rowId + ".message",
                messageSlice.text,
                messageSlice.x,
                0.0f,
                messageSlice.width,
                kLogRowHeight,
                13.5f,
                visual.message,
                visual.messageWeight,
                "monospace");
        })
        .build();
}

void composeHorizontalScroll(
    eui::Ui& ui,
    const float x,
    const float y,
    const float width,
    const float maximumOffset,
    const float viewportWidth,
    const float contentWidth) {
    WorkbenchState& workbench = state();
    const float thumbWidth = std::clamp(
        width * (viewportWidth / std::max(viewportWidth, contentWidth)),
        42.0f,
        width);
    const float travel = std::max(0.0f, width - thumbWidth);
    const float offset = std::clamp(workbench.horizontalScroll.get(), 0.0f, maximumOffset);
    const float thumbX = maximumOffset > 0.0f ? travel * (offset / maximumOffset) : 0.0f;

    ui.stack("logs.horizontal")
        .position(x, y)
        .size(width, kHorizontalScrollHeight)
        .content([&] {
            ui.rect("logs.horizontal.track")
                .position(0.0f, 5.0f)
                .size(width, 6.0f)
                .radius(3.0f)
                .color({1.0f, 1.0f, 1.0f, 0.07f})
                .onPress([maximumOffset, thumbWidth, width](
                             const eui::PointerEvent& event,
                             const eui::Rect& bounds) {
                    if (maximumOffset <= 0.0f || width <= 0.0f || bounds.width <= 0.0f) {
                        return;
                    }
                    const float localX = static_cast<float>(event.x) - bounds.x;
                    const float thumbPixels = bounds.width * (thumbWidth / width);
                    const float travelPixels = std::max(0.0f, bounds.width - thumbPixels);
                    if (travelPixels <= 0.0f) {
                        return;
                    }
                    const float thumbPosition = std::clamp(
                        localX - thumbPixels * 0.5f,
                        0.0f,
                        travelPixels);
                    state().horizontalScroll.set(
                        thumbPosition / travelPixels * maximumOffset);
                })
                .build();
            ui.rect("logs.horizontal.thumb")
                .position(thumbX, 4.0f)
                .size(thumbWidth, 8.0f)
                .states(
                    {1.0f, 1.0f, 1.0f, 0.28f},
                    {1.0f, 1.0f, 1.0f, 0.42f},
                    {1.0f, 1.0f, 1.0f, 0.58f})
                .radius(4.0f)
                .onPress([thumbWidth, travel](
                             const eui::PointerEvent&,
                             const eui::Rect& bounds) {
                    state().horizontalDragStart = state().horizontalScroll.get();
                    state().horizontalDragTravelPixels = thumbWidth > 0.0f
                        ? travel * (bounds.width / thumbWidth)
                        : 0.0f;
                })
                .onDrag([maximumOffset](const core::dsl::DragEvent& event) {
                    const float travelPixels = state().horizontalDragTravelPixels;
                    if (maximumOffset <= 0.0f || travelPixels <= 0.0f) {
                        return;
                    }
                    state().horizontalScroll.set(std::clamp(
                        state().horizontalDragStart
                            + static_cast<float>(event.totalX) / travelPixels * maximumOffset,
                        0.0f,
                        maximumOffset));
                })
                .build();
        })
        .build();
}

void setVerticalLogScroll(const float value, const float maximumOffset) {
    WorkbenchState& workbench = state();
    const float nextOffset = std::clamp(value, 0.0f, maximumOffset);
    workbench.logScroll.set(nextOffset);
    workbench.follow.set(
        maximumOffset <= 1.0f || nextOffset >= maximumOffset - 1.0f);
    app::requestUpdate();
}

void composeVerticalLogScroll(
    eui::Ui& ui,
    const float x,
    const float height,
    const float maximumOffset,
    const float viewportHeight,
    const float contentHeight) {
    constexpr float scrollbarWidth = 8.0f;
    const float thumbHeight = std::clamp(
        height * (viewportHeight / std::max(viewportHeight, contentHeight)),
        std::min(42.0f, height),
        height);
    const float travel = std::max(0.0f, height - thumbHeight);
    const float offset = std::clamp(state().logScroll.get(), 0.0f, maximumOffset);
    const float thumbY = maximumOffset > 0.0f ? travel * (offset / maximumOffset) : 0.0f;

    ui.stack("logs.vertical")
        .position(x, 0.0f)
        .size(scrollbarWidth, height)
        .content([&] {
            ui.rect("logs.vertical.track")
                .position(1.0f, 0.0f)
                .size(6.0f, height)
                .radius(3.0f)
                .color({1.0f, 1.0f, 1.0f, 0.07f})
                .onPress([maximumOffset, thumbHeight, height](
                             const eui::PointerEvent& event,
                             const eui::Rect& bounds) {
                    if (maximumOffset <= 0.0f || height <= 0.0f || bounds.height <= 0.0f) {
                        return;
                    }
                    const float localY = static_cast<float>(event.y) - bounds.y;
                    const float thumbPixels = bounds.height * (thumbHeight / height);
                    const float travelPixels = std::max(0.0f, bounds.height - thumbPixels);
                    if (travelPixels <= 0.0f) {
                        return;
                    }
                    const float thumbPosition = std::clamp(
                        localY - thumbPixels * 0.5f,
                        0.0f,
                        travelPixels);
                    setVerticalLogScroll(
                        thumbPosition / travelPixels * maximumOffset,
                        maximumOffset);
                })
                .build();
            ui.rect("logs.vertical.thumb")
                .position(0.0f, thumbY)
                .size(scrollbarWidth, thumbHeight)
                .states(
                    {1.0f, 1.0f, 1.0f, 0.28f},
                    {1.0f, 1.0f, 1.0f, 0.42f},
                    {1.0f, 1.0f, 1.0f, 0.58f})
                .radius(4.0f)
                .onPress([thumbHeight, travel](
                             const eui::PointerEvent&,
                             const eui::Rect& bounds) {
                    state().verticalDragStart = state().logScroll.get();
                    state().verticalDragTravelPixels = thumbHeight > 0.0f
                        ? travel * (bounds.height / thumbHeight)
                        : 0.0f;
                })
                .onDrag([maximumOffset](const core::dsl::DragEvent& event) {
                    const float travelPixels = state().verticalDragTravelPixels;
                    if (maximumOffset <= 0.0f || travelPixels <= 0.0f) {
                        return;
                    }
                    setVerticalLogScroll(
                        state().verticalDragStart
                            + static_cast<float>(event.totalY) / travelPixels * maximumOffset,
                        maximumOffset);
                })
                .build();
        })
        .build();
}

void composeLogList(
    eui::Ui& ui,
    const float width,
    const float height,
    const std::size_t itemCount,
    const float maximumVerticalOffset,
    const float maximumHorizontalOffset) {
    constexpr float scrollbarWidth = 8.0f;
    constexpr float scrollbarGap = 4.0f;
    constexpr float overscanViewports = 0.75f;
    const float contentWidth = std::max(0.0f, width - scrollbarWidth - scrollbarGap);
    const float totalHeight = static_cast<float>(itemCount) * kLogRowHeight;
    const float currentOffset = std::clamp(
        state().logScroll.get(),
        0.0f,
        maximumVerticalOffset);
    const float overscanPixels = height * overscanViewports;
    const float firstPixel = std::max(0.0f, currentOffset - overscanPixels);
    const float lastPixel = std::min(
        totalHeight,
        currentOffset + height + overscanPixels);
    const std::size_t firstIndex = std::min(
        itemCount,
        static_cast<std::size_t>(firstPixel / kLogRowHeight));
    const std::size_t lastIndex = std::min(
        itemCount,
        static_cast<std::size_t>(std::ceil(lastPixel / kLogRowHeight)) + 1);

    ui.stack("logs.list")
        .size(width, height)
        .clip()
        .focusable()
        .onPress([currentOffset, height, itemCount](
                     const eui::PointerEvent& event,
                     const eui::Rect& bounds) {
            if (bounds.height <= 0.0f || itemCount == 0) {
                return;
            }
            const float localY = std::clamp(
                (static_cast<float>(event.y) - bounds.y) * (height / bounds.height),
                0.0f,
                std::max(0.0f, height - 0.001f));
            const float contentY = currentOffset + localY;
            if (contentY >= static_cast<float>(itemCount) * kLogRowHeight) {
                return;
            }
            const std::size_t visibleIndex = std::min(
                itemCount - 1,
                static_cast<std::size_t>(contentY / kLogRowHeight));
            WorkbenchState& workbench = state();
            const auto& indices = workbench.backend.filteredLogIndices(
                workbench.search.get(),
                selectedFilter(workbench.filter.get()),
                workbench.selectedSession);
            if (visibleIndex >= indices.size()) {
                return;
            }
            const std::size_t logIndex = indices[visibleIndex];
            const auto& logs = workbench.backend.logs();
            if (logIndex < logs.size()) {
                workbench.selectedLogIndex = logIndex;
                workbench.selectedLogMessage = logs[logIndex].message;
                app::requestUpdate();
            }
        })
        .onTextInput([](const core::KeyboardEvent& event) {
            if (event.hasShortcut(core::InputKey::C)
                && !state().selectedLogMessage.empty()) {
                core::window::setClipboardText(state().selectedLogMessage);
            }
        })
        .onScroll([maximumVerticalOffset, maximumHorizontalOffset](
                      const core::ScrollEvent& event) {
            if (std::fabs(event.x) > 0.001 && maximumHorizontalOffset > 0.0f) {
                WorkbenchState& workbench = state();
                workbench.horizontalScroll.set(std::clamp(
                    workbench.horizontalScroll.get()
                        - static_cast<float>(event.x) * kLogRowHeight * 2.0f,
                    0.0f,
                    maximumHorizontalOffset));
                app::requestUpdate();
                return;
            }
            if (std::fabs(event.y) > 0.001) {
                setVerticalLogScroll(
                    state().logScroll.get()
                        - static_cast<float>(event.y) * kLogRowHeight * 2.0f,
                    maximumVerticalOffset);
            }
        })
        .cursor(core::CursorShape::Arrow)
        .content([&] {
            ui.stack("logs.list.window")
                .size(contentWidth, height)
                .content([&] {
                    std::size_t slot = 0;
                    for (std::size_t index = firstIndex; index < lastIndex; ++index) {
                        const std::string rowId =
                            "logs.list.slot." + std::to_string(slot);
                        ui.stack(rowId)
                            .y(static_cast<float>(index) * kLogRowHeight - currentOffset)
                            .size(contentWidth, kLogRowHeight)
                            .content([&] {
                                composeLogRow(
                                    ui,
                                    rowId,
                                    static_cast<std::int64_t>(index),
                                    contentWidth,
                                    kLogRowHeight);
                            })
                            .build();
                        ++slot;
                    }
                })
                .build();

            if (maximumVerticalOffset > 0.0f) {
                composeVerticalLogScroll(
                    ui,
                    width - scrollbarWidth,
                    height,
                    maximumVerticalOffset,
                    height,
                    totalHeight);
            }
        })
        .build();
}

void composeEmptyState(
    eui::Ui& ui,
    const float width,
    const float height,
    const bool filtered) {
    WorkbenchState& workbench = state();
    std::string title = "Waiting for an MCDevLink session";
    std::string subtitle = workbench.backend.isRunning()
        ? "Safaia receiver listening on " + endpointText(workbench.backend.localEndpoint())
        : workbench.backend.startError();
    if (filtered) {
        title = "No matching log lines";
        subtitle = "The current session, search, and level filters returned no results.";
    } else if (!workbench.backend.logs().empty()) {
        title = "No logs for this session";
        subtitle = "The selected session has not produced any buffered log lines.";
    }

    const float centerY = std::max(60.0f, height * 0.42f);
    ui.rect("empty.icon.background")
        .position((width - 48.0f) * 0.5f, centerY - 56.0f)
        .size(48.0f, 48.0f)
        .radius(6.0f)
        .color(kRaised)
        .border(1.0f, kBorder)
        .build();
    composeTerminalGlyph(
        ui,
        "empty.icon.glyph",
        (width - 48.0f) * 0.5f,
        centerY - 56.0f,
        48.0f,
        kMuted);
    ui.text("empty.title")
        .position(30.0f, centerY + 6.0f)
        .size(std::max(0.0f, width - 60.0f), 32.0f)
        .text(title)
        .fontSize(17.0f)
        .fontWeight(650)
        .color(kText)
        .horizontalAlign(eui::HorizontalAlign::Center)
        .verticalAlign(eui::VerticalAlign::Center)
        .build();
    ui.text("empty.subtitle")
        .position(50.0f, centerY + 40.0f)
        .size(std::max(0.0f, width - 100.0f), 28.0f)
        .text(subtitle)
        .fontSize(13.0f)
        .color(kMuted)
        .horizontalAlign(eui::HorizontalAlign::Center)
        .verticalAlign(eui::VerticalAlign::Center)
        .build();
}

void composeMain(eui::Ui& ui, const float width, const float height) {
    WorkbenchState& workbench = state();
    if (workbench.selectedSession.has_value()
        && workbench.backend.findSession(*workbench.selectedSession) == nullptr) {
        workbench.selectedSession.reset();
    }
    const auto& visible = workbench.backend.filteredLogIndices(
        workbench.search.get(),
        selectedFilter(workbench.filter.get()),
        workbench.selectedSession);

    const mcdev::SessionSummary* selected = workbench.selectedSession.has_value()
        ? workbench.backend.findSession(*workbench.selectedSession)
        : nullptr;
    const std::string title = selected == nullptr ? "All sessions" : sessionTitle(*selected);
    const std::string subtitle = std::to_string(visible.size()) + " visible lines  |  "
        + std::to_string(workbench.backend.readySessionCount()) + " ready sessions";
    const float toolbarY = kHeaderHeight;
    const float columnsY = toolbarY + kToolbarHeight;
    const float listY = columnsY + kColumnHeaderHeight;
    const float messageX = kLogLeft + kLevelColumnWidth;
    const float messageViewportWidth = std::max(0.0f, width - messageX - 22.0f);
    const float messageContentWidth = std::max(
        messageViewportWidth,
        std::min(
            50000.0f,
            static_cast<float>(workbench.backend.maximumMessageBytes()) * 8.25f + 20.0f));
    const float maximumHorizontalOffset =
        std::max(0.0f, messageContentWidth - messageViewportWidth);
    const bool horizontalScrollable = !visible.empty() && maximumHorizontalOffset > 0.5f;
    const float horizontalHeight = horizontalScrollable ? kHorizontalScrollHeight : 0.0f;
    const float listHeight =
        std::max(0.0f, height - listY - kFooterHeight - horizontalHeight);
    const float searchWidth = std::clamp(width * 0.31f, 205.0f, 340.0f);
    const float segmentWidth = std::clamp(width * 0.27f, 205.0f, 250.0f);
    const float clearX = width - 68.0f;
    const float followX = clearX - 124.0f;

    ui.stack("main")
        .size(width, height)
        .clip()
        .content([&] {
            ui.rect("main.background").size(width, height).color(kCanvas).build();

            ui.rect("header.background")
                .size(width, kHeaderHeight)
                .color(kBand)
                .build();
            ui.rect("header.border")
                .position(0.0f, kHeaderHeight - 1.0f)
                .size(width, 1.0f)
                .color(kBorder)
                .build();
            clippedText(ui, "header.title", title, 24.0f, 15.0f, width - 220.0f, 32.0f, 22.0f, kText, 720);
            clippedText(ui, "header.subtitle", subtitle, 24.0f, 52.0f, width - 220.0f, 22.0f, 13.0f, kMuted);

            const bool connected = workbench.backend.readySessionCount() > 0;
            const bool running = workbench.backend.isRunning();
            ui.rect("header.health.dot")
                .position(width - 159.0f, 36.0f)
                .size(8.0f, 8.0f)
                .radius(4.0f)
                .color(connected || running ? kStatusGreen : kError)
                .build();
            clippedText(
                ui,
                "header.health",
                connected ? "CONNECTED" : (running ? "LISTENING" : "OFFLINE"),
                width - 141.0f,
                24.0f,
                116.0f,
                32.0f,
                11.5f,
                connected || running ? kMuted : kError,
                700);

            ui.rect("toolbar.background")
                .position(0.0f, toolbarY)
                .size(width, kToolbarHeight)
                .color(kCanvas)
                .build();
            ui.rect("toolbar.border")
                .position(0.0f, columnsY - 1.0f)
                .size(width, 1.0f)
                .color(kBorder)
                .build();

            components::InputStyle inputStyle(theme());
            inputStyle.background = kBand;
            inputStyle.focused = kRaised;
            inputStyle.border = kBorder;
            inputStyle.focusBorder = kMuted;
            inputStyle.placeholder = kFaint;
            inputStyle.shadow = {};
            inputStyle.radius = 6.0f;
            components::input(ui, "toolbar.search")
                .position(24.0f, toolbarY + 14.0f)
                .size(searchWidth, 42.0f)
                .placeholder("Search logs")
                .fontSize(14.0f)
                .inset(14.0f)
                .bind(workbench.search)
                .style(inputStyle)
                .transition(transition())
                .build();

            components::SegmentedStyle segmentedStyle(theme());
            segmentedStyle.background = kBand;
            segmentedStyle.hover = kHover;
            segmentedStyle.selected = kText;
            segmentedStyle.text = kMuted;
            segmentedStyle.selectedText = kCanvas;
            segmentedStyle.border = kBorder;
            ui.stack("toolbar.filters.wrap")
                .position(36.0f + searchWidth, toolbarY + 14.0f)
                .size(segmentWidth, 42.0f)
                .content([&] {
                    components::segmented(ui, "toolbar.filters")
                        .size(segmentWidth, 42.0f)
                        .items({"All", "Warnings", "Errors"})
                        .fontSize(13.0f)
                        .bind(workbench.filter)
                        .style(segmentedStyle)
                        .transition(transition())
                        .build();
                })
                .build();

            components::SwitchStyle switchStyle(theme());
            switchStyle.off = kPressed;
            switchStyle.on = kText;
            switchStyle.knob = workbench.follow.get() ? kCanvas : kMuted;
            switchStyle.text = kMuted;
            switchStyle.rowHover = {1.0f, 1.0f, 1.0f, 0.04f};
            switchStyle.rowPressed = {1.0f, 1.0f, 1.0f, 0.08f};
            ui.stack("toolbar.follow.wrap")
                .position(followX, toolbarY + 14.0f)
                .size(116.0f, 42.0f)
                .content([&] {
                    components::toggleSwitch(ui, "toolbar.follow")
                        .size(116.0f, 42.0f)
                        .trackSize(36.0f, 20.0f)
                        .fontSize(13.0f)
                        .text("Follow")
                        .bind(workbench.follow)
                        .style(switchStyle)
                        .transition(transition())
                        .build();
                })
                .build();

            components::ButtonStyle clearStyle(theme(), false);
            clearStyle.normal = kBand;
            clearStyle.hover = kHover;
            clearStyle.pressed = kPressed;
            clearStyle.text = kMuted;
            clearStyle.icon = kMuted;
            clearStyle.border = {1.0f, kBorder};
            clearStyle.shadow = {};
            clearStyle.radius = 6.0f;
            components::button(ui, "toolbar.clear")
                .position(clearX, toolbarY + 14.0f)
                .size(44.0f, 42.0f)
                .text("")
                .style(clearStyle)
                .transition(transition())
                .onClick([] {
                    state().backend.clearLogs();
                    state().logScroll.set(0.0f);
                    state().selectedLogIndex.reset();
                    state().selectedLogMessage.clear();
                })
                .build();
            composeClearGlyph(
                ui,
                "toolbar.clear.glyph",
                clearX,
                toolbarY + 14.0f,
                kMuted);
            components::tooltip(ui, "toolbar.clear.tooltip")
                .source("toolbar.clear.bg")
                .value("Clear logs")
                .anchor(clearX + 22.0f, toolbarY + 13.0f)
                .bounds(width, height)
                .theme(theme())
                .build();

            ui.rect("columns.background")
                .position(0.0f, columnsY)
                .size(width, kColumnHeaderHeight)
                .color(kBand)
                .build();
            ui.rect("columns.border")
                .position(0.0f, listY - 1.0f)
                .size(width, 1.0f)
                .color(kBorder)
                .build();
            clippedText(ui, "columns.level", "LEVEL", kLogLeft, columnsY, kLevelColumnWidth - 8.0f, kColumnHeaderHeight, 11.0f, kMuted, 700);
            clippedText(ui, "columns.message", "MESSAGE", messageX, columnsY, messageViewportWidth, kColumnHeaderHeight, 11.0f, kMuted, 700);

            if (visible.empty()) {
                const bool filtered = !workbench.search.get().empty() || workbench.filter.get() != 0;
                ui.stack("logs.empty")
                    .position(0.0f, listY)
                    .size(width, listHeight)
                    .content([&] { composeEmptyState(ui, width, listHeight, filtered); })
                    .build();
            } else {
                const float maximumVerticalOffset = std::max(
                    0.0f,
                    static_cast<float>(visible.size()) * kLogRowHeight - listHeight);
                if (workbench.follow.get()) {
                    workbench.logScroll.set(maximumVerticalOffset);
                }
                ui.stack("logs.list.position")
                    .position(0.0f, listY)
                    .size(width, listHeight)
                    .content([&] {
                        composeLogList(
                            ui,
                            width,
                            listHeight,
                            visible.size(),
                            maximumVerticalOffset,
                            maximumHorizontalOffset);
                    })
                    .build();
            }

            if (horizontalScrollable) {
                workbench.horizontalScroll.set(std::clamp(
                    workbench.horizontalScroll.get(),
                    0.0f,
                    maximumHorizontalOffset));
                composeHorizontalScroll(
                    ui,
                    messageX,
                    listY + listHeight,
                    messageViewportWidth,
                    maximumHorizontalOffset,
                    messageViewportWidth,
                    messageContentWidth);
            } else {
                workbench.horizontalScroll.set(0.0f);
            }

            const float footerY = height - kFooterHeight;
            ui.rect("footer.background")
                .position(0.0f, footerY)
                .size(width, kFooterHeight)
                .color(kBand)
                .build();
            ui.rect("footer.border")
                .position(0.0f, footerY)
                .size(width, 1.0f)
                .color(kBorder)
                .build();
            clippedText(
                ui,
                "footer.protocol",
                "MCDevLink / Safaia",
                22.0f,
                footerY,
                150.0f,
                kFooterHeight,
                11.5f,
                kMuted,
                650);
            clippedText(
                ui,
                "footer.diagnostic",
                workbench.backend.lastDiagnostic().empty()
                    ? (workbench.backend.isRunning() ? "Receiver active" : workbench.backend.startError())
                    : workbench.backend.lastDiagnostic(),
                178.0f,
                footerY,
                std::max(0.0f, width - 200.0f),
                kFooterHeight,
                11.5f,
                kFaint);
        })
        .build();
}

} // namespace

void compose(eui::Ui& ui, const eui::Screen& screen) {
    mcdev::platform::applyWindowTheme();
    WorkbenchState& workbench = state();
    (void)workbench.backend.start([] { app::requestUpdate(); });
    workbench.backend.drainPendingEvents();

    const float defaultSidebarWidth = screen.width >= 1020.0f
        ? 252.0f
        : (screen.width >= 900.0f ? 240.0f : 212.0f);
    const float maximumSidebarWidth = std::max(
        kSidebarMinimumWidth,
        std::min(kSidebarMaximumWidth, screen.width - kMainMinimumWidth));
    if (workbench.sidebarWidth <= 0.0f) {
        workbench.sidebarWidth = defaultSidebarWidth;
    }
    workbench.sidebarWidth = std::clamp(
        workbench.sidebarWidth,
        kSidebarMinimumWidth,
        maximumSidebarWidth);
    const float sidebarWidth = workbench.sidebarWidth;
    const float mainWidth = std::max(0.0f, screen.width - sidebarWidth);

    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.row("root.layout")
                .size(screen.width, screen.height)
                .content([&] {
                    composeSidebar(ui, sidebarWidth, screen.height);
                    composeMain(ui, mainWidth, screen.height);
                })
                .build();

            ui.rect("root.splitter")
                .position(sidebarWidth - kSplitterHitWidth * 0.5f, 0.0f)
                .size(kSplitterHitWidth, screen.height)
                .states(
                    {1.0f, 1.0f, 1.0f, 0.0f},
                    {1.0f, 1.0f, 1.0f, 0.08f},
                    {1.0f, 1.0f, 1.0f, 0.14f})
                .instantStates()
                .onPress([sidebarWidth](
                             const eui::PointerEvent&,
                             const eui::Rect& bounds) {
                    WorkbenchState& current = state();
                    current.sidebarDragStart = sidebarWidth;
                    current.sidebarDragLogicalPerPixel = bounds.width > 0.0f
                        ? kSplitterHitWidth / bounds.width
                        : 1.0f;
                })
                .onDrag([maximumSidebarWidth](const core::dsl::DragEvent& event) {
                    WorkbenchState& current = state();
                    current.sidebarWidth = std::clamp(
                        current.sidebarDragStart
                            + static_cast<float>(event.totalX)
                                * current.sidebarDragLogicalPerPixel,
                        kSidebarMinimumWidth,
                        maximumSidebarWidth);
                })
                .build();
        })
        .build();

}

} // namespace app
