#pragma once
#include <functional>
#include <string>

namespace perlite {

    enum class LogLevel : int { Debug, Info, Warn, Error };

    using LogSink = std::function<void(LogLevel, const std::string&)>;
    void SetLogSink(LogSink sink);
    void Log(LogLevel level, const std::string& text);

    // Lines look like the Java server's: "[12:34:56] [Server thread/INFO]: text". Each thread
    // names itself once; unnamed threads show as "Worker".
    void SetLogThreadName(const std::string& name);
    // Also writes every line to <dir>/latest.log. A previous latest.log is archived first as
    // <dir>/YYYY-MM-DD-N.log.gz, the way the Java server keeps its logs.
    bool OpenLogFile(const std::string& dir);
    void CloseLogFile();
    // Debug lines are dropped unless this is on.
    void SetDebugLogging(bool on);

    class LogCapture {
    public:
        LogCapture();
        ~LogCapture();
        LogCapture(const LogCapture&) = delete;
        LogCapture& operator=(const LogCapture&) = delete;
        const std::string& Text() const { return text; }

    private:
        std::string text;
        std::string* previous = nullptr;
    };

    inline void LogDebug(const std::string& text) {
        Log(LogLevel::Debug, text);
    }
    inline void LogInfo(const std::string& text) {
        Log(LogLevel::Info, text);
    }
    inline void LogWarn(const std::string& text) {
        Log(LogLevel::Warn, text);
    }
    inline void LogError(const std::string& text) {
        Log(LogLevel::Error, text);
    }
}
