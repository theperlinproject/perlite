#include "common/Log.hpp"
#include "common/Strings.hpp"

#include <miniz.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace perlite {
    namespace {
        std::mutex g_mutex;
        LogSink g_sink;
        std::FILE* g_file = nullptr;
        std::atomic<bool> g_debug{false};
        thread_local std::string* g_capture = nullptr;
        thread_local std::string g_threadName;

        std::tm LocalTime(std::time_t t) {
            std::tm out{};
#ifdef _WIN32
            localtime_s(&out, &t);
#else
            localtime_r(&t, &out);
#endif
            return out;
        }

        // A .gz file: header, raw deflate stream, crc32 and length (RFC 1952).
        bool GzipFile(const std::filesystem::path& from, const std::filesystem::path& to) {
            std::ifstream in(from, std::ios::binary);
            if (!in)
                return false;
            const std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            size_t packedSize = 0;
            void* packed = tdefl_compress_mem_to_heap(data.data(), data.size(), &packedSize,
                                                      TDEFL_DEFAULT_MAX_PROBES | TDEFL_GREEDY_PARSING_FLAG);
            if (!packed && !data.empty())
                return false;
            std::ofstream out(to, std::ios::binary | std::ios::trunc);
            const unsigned char header[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
            out.write(reinterpret_cast<const char*>(header), sizeof header);
            if (packed)
                out.write(static_cast<const char*>(packed), static_cast<std::streamsize>(packedSize));
            mz_free(packed);
            const mz_ulong crc =
                mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data.data()), data.size());
            const uint32_t size = static_cast<uint32_t>(data.size());
            for (const uint32_t v : {static_cast<uint32_t>(crc), size})
                for (int i = 0; i < 4; i++)
                    out.put(static_cast<char>(v >> (8 * i)));
            return out.good();
        }
    }

    LogCapture::LogCapture() : previous(g_capture) {
        g_capture = &text;
    }
    LogCapture::~LogCapture() {
        g_capture = previous;
    }

    void SetLogSink(LogSink sink) {
        std::lock_guard lock(g_mutex);
        g_sink = std::move(sink);
    }

    void SetLogThreadName(const std::string& name) {
        g_threadName = name;
    }

    void SetDebugLogging(bool on) {
        g_debug = on;
    }

    bool OpenLogFile(const std::string& dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path latest = fs::path(dir) / "latest.log";
        if (fs::exists(latest, ec) && fs::file_size(latest, ec) > 0) {
            const auto written = fs::last_write_time(latest, ec);
            const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                written - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            const std::tm day = LocalTime(std::chrono::system_clock::to_time_t(sys));
            char date[16];
            std::strftime(date, sizeof date, "%Y-%m-%d", &day);
            for (int n = 1; n < 10000; n++) {
                const fs::path archive = fs::path(dir) / (std::string(date) + "-" + std::to_string(n) + ".log.gz");
                if (fs::exists(archive, ec))
                    continue;
                if (GzipFile(latest, archive))
                    fs::remove(latest, ec);
                break;
            }
        }
        std::lock_guard lock(g_mutex);
        if (g_file)
            std::fclose(g_file);
        g_file = std::fopen(latest.string().c_str(), "ab");
        return g_file != nullptr;
    }

    void CloseLogFile() {
        std::lock_guard lock(g_mutex);
        if (g_file)
            std::fclose(g_file);
        g_file = nullptr;
    }

    void Log(LogLevel level, const std::string& text) {
        if (level == LogLevel::Debug && !g_debug)
            return;
        if (g_capture && level == LogLevel::Info) {
            *g_capture += str::StripFormatting(text);
            *g_capture += '\n';
            return;
        }
        std::lock_guard lock(g_mutex);
        if (g_sink) {
            g_sink(level, text);
            return;
        }
        static const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        const std::tm now = LocalTime(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
        char stamp[16];
        std::strftime(stamp, sizeof stamp, "%H:%M:%S", &now);
        const std::string& thread = g_threadName.empty() ? std::string("Worker") : g_threadName;
        const std::string plain = str::StripFormatting(text);
        std::FILE* console = level >= LogLevel::Warn ? stderr : stdout;
        std::fprintf(console, "[%s] [%s/%s]: %s\n", stamp, thread.c_str(), names[static_cast<int>(level)],
                     plain.c_str());
        std::fflush(console);
        if (g_file) {
            std::fprintf(g_file, "[%s] [%s/%s]: %s\n", stamp, thread.c_str(), names[static_cast<int>(level)],
                         plain.c_str());
            std::fflush(g_file);
        }
    }
}
