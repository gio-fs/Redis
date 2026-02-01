#pragma once
#include <string>
#include <fstream>
#include <format>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <concepts>
#include <ctime>

enum class LogLevel {
    Debug, 
    Info, 
    Warning, 
    Error
};

enum class LogMode {
    Enum,
    Match,
    JSON,
    Normal
};

// using concepts instead of sfinae
template<typename T>
concept NotLogMode = !std::same_as<std::decay_t<T>, LogMode>;

// generalizing the stream where the log messages
// can be written (eg. console, files, memory)
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void write(const std::string &msg) = 0; // pure virtual
};

class ConsoleSink : public LogSink {
public:
    ConsoleSink() = default;
    void write(const std::string &msg) override;
};

class FileSink : public LogSink {
    std::ofstream out;

public:
    FileSink() = default;
    FileSink(const std::string &path);

    void write(const std::string &msg) override;
};

class MemorySink : public LogSink {
    std::vector<std::string> membuf;

public:
    MemorySink() = default;
    void write(const std::string &msg) override;
};

class Logger {
    std::vector<std::unique_ptr<LogSink>> sinks_ {}; // multiple sinks
    LogLevel min_level {LogLevel::Debug};
    bool enabled {true};
    Logger() {}

    public: 
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    Logger(Logger& logger) = delete;
    Logger& operator=(Logger& logger) = delete;
    Logger(Logger&& logger) = delete;
    Logger&& operator=(Logger&& logger) = delete;

    // defined here in the header because 
    // it's used by the variadic templates later
    std::string_view to_string(LogLevel level) {
        switch (level) {
            case LogLevel::Debug:   return "DEBUG";
            case LogLevel::Info:    return "INFO";
            case LogLevel::Warning: return "WARN";
            case LogLevel::Error:   return "ERROR";
        }

        return "UNKNOWN";
    }

    bool is_enabled();
    void enable();
    void disable();

    void set_min_level(LogLevel level) {
        min_level = level;
    }

    std::string get_timespec() {
        std::time_t t = std::time(nullptr);
        char mbstr[32];
        std::strftime(mbstr, sizeof(mbstr), "%F %T", std::localtime(&t));
        return mbstr; // implicit conversion to string & alloc on heap
    }

    std::string extract_fn_name(const char* signature) {
        const char* end = signature + strlen(signature);
        const char* cur = end;
        std::string name;

        while (*cur != '(') cur--;
        while (--cur > signature && !std::isspace(static_cast<unsigned char>(*cur))) {
            name += *cur;
        }

        std::reverse(name.begin(), name.end());
        return name;
    }

    template<typename... Args>
    void add_sinks(Args&&... sinks) {
        auto push_sink = [this](auto&& sink) { // explicit capture of this
            sinks_.push_back(std::move(sink));
        };  

        // preserve r/l-valueness
        (push_sink(std::forward<Args>(sinks)), ...); // call lambda on every sink
    }


    template<typename... Args>
    std::string format_message(
        LogLevel level, std::string_view caller, LogMode mode, Args&&... args) {
        std::string fmt = "[{}] [{}] {}(): "; // reserve format for timestamp level and fn name
        std::string_view level_string = to_string(level);
        std::string now = get_timespec();
        // printf("%s, %d", now.data(), now.length());

        if (sizeof...(args) == 0) {
            return std::vformat(fmt, std::make_format_args(now, level_string, caller));
        }

        if (mode == LogMode::Match) {
            for (size_t i {}; i < sizeof...(args); i += 2) {
                fmt += "\n--- {}: ";
                fmt += "{}";
            } 
        } else if (mode == LogMode::Enum) {
            for (size_t i {}; i < sizeof...(args) - 1; i++) {
                fmt += "\n--- {},";
            }
            fmt += "{}";
        } else if (mode == LogMode::JSON) {
            fmt += "\n{\n";
            for (size_t i {}; i < sizeof...(args); i += 2) {
                fmt += "\t{} : {},\n";
            }
            fmt += "}";
        } else { // normal mode
            for (size_t i {}; i < sizeof...(args) - 1; i++) {
                fmt += "{} ";
            }
            fmt += "{}";
        }
        
        // vformat accepts runtime format string and args
        return std::vformat(fmt, std::make_format_args(now, level_string, caller, args...));
    }

    // using wrapper with concept for optional log mode
    template<NotLogMode Arg, typename... Args>
    void log_info(std::string_view caller, Arg&& arg, Args&&... args) {
        return log(LogLevel::Info, caller, LogMode::Normal, std::forward<Arg>(arg), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_info(std::string_view caller, LogMode mode, Args&&... args) {
        return log(LogLevel::Info, caller, mode, std::forward<Args>(args)...);
    }

    template<NotLogMode Arg, typename... Args>
    void log_debug(std::string_view caller, Arg&& arg, Args&&... args) {
        return log(LogLevel::Debug, caller, LogMode::Normal, std::forward<Arg>(arg), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_debug(std::string_view caller, LogMode mode, Args&&... args) {
        return log(LogLevel::Debug, caller, mode, std::forward<Args>(args)...);
    }
    
    template<NotLogMode Arg, typename... Args>
    void log_error(std::string_view caller, Arg&& arg, Args&&... args) {
        return log(LogLevel::Error, caller, LogMode::Normal, std::forward<Arg>(arg), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_error(std::string_view caller, LogMode mode, Args&&... args) {
        return log(LogLevel::Error, caller, mode, std::forward<Args>(args)...);
    }

    template<NotLogMode Arg, typename... Args>
    void log_warn(std::string_view caller, Arg&& arg, Args&&... args) {
        return log(LogLevel::Warning, caller, LogMode::Normal, std::forward<Arg>(arg), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_warn(std::string_view caller, LogMode mode, Args&&... args) {
        return log(LogLevel::Warning, caller, mode, std::forward<Args>(args)...);
    }

    
    void log_json(std::string_view caller, 
        std::initializer_list<std::pair<std::string_view, std::string_view>>& fields) {
        return log(LogLevel::Info, caller, LogMode::JSON, fields);
    }

    template<typename... Args>
    void log(LogLevel level, std::string_view caller, LogMode mode, Args&&... args) {
        if (!is_enabled()) return;
        if (level < min_level) return; 

        std::string msg = format_message(level, caller, mode, args...);
        for (auto& sink : sinks_) {
            sink->write(msg);
        }
    }

    template<typename... Args>
    std::string format_message_custom(LogLevel level, std::string_view fmt, Args&&... args) {
        std::string base_fmt = "[{}] [{}] ";
        base_fmt += fmt;
        std::string_view level_str = to_string(level);
        std::string time = get_timespec();
        return std::vformat(base_fmt, std::make_format_args(time, level_str, args...));
    }

    template<typename... Args>
    void custom_log(LogLevel level, std::string_view fmt, Args&&... args) {
        if (!is_enabled()) return;  
        if (level < min_level) return; 

        std::string msg = format_message_custom(level, fmt, args...);
        for (auto& sink : sinks_) {
            sink->write(msg);
        }
    }
};

#define LOG_DEBUG(...) \
    Logger::instance().log_debug( \
        std::string_view(__FUNCTION__), \
        __VA_ARGS__ \
    )
#define LOG_INFO(...) \
    Logger::instance().log_info( \
        std::string_view(__FUNCTION__), \
        __VA_ARGS__ \
    )
#define LOG_WARN(...) \
    Logger::instance().log_warn( \
        std::string_view(__FUNCTION__), \
        __VA_ARGS__ \
    )
#define LOG_ERROR(...) \
    Logger::instance().log_error( \
        std::string_view(__FUNCTION__), \
        __VA_ARGS__ \
    )
#define LOG_JSON(...) \
    Logger::instance().log_json( \
        std::string_view(__FUNCTION__), \
        __VA_ARGS__ \
    )

#define LOG_INFO_FMT(fmt, ...) Logger::instance().custom_log(LogLevel::Info, fmt, __VA_ARGS__)
#define LOG_ERROR_FMT(fmt, ...) Logger::instance().custom_log(LogLevel::Error, fmt, __VA_ARGS__)
#define LOG_DEBUG_FMT(fmt, ...) Logger::instance().custom_log(LogLevel::Debug, fmt, __VA_ARGS__)

inline Logger& logger = Logger::instance(); 
