#include <iostream>
#include <memory>
#include <vector>
#include <source_location>

#include "logger.h"

bool Logger::is_enabled() {
    return enabled;
}

void Logger::enable() {
    enabled = true;
}

void Logger::disable() {
    enabled = false;
}

void ConsoleSink::write(const std::string &msg) {
    std::cout << msg << '\n';
}

void MemorySink::write(const std::string &msg) {
    membuf.push_back(msg);
}

FileSink::FileSink(const std::string &path)
    : out(path, std::ios::app) {} // open with append flag

void FileSink::write(const std::string &msg)  {
    out << msg << '\n'; // we dont use std::endl, flush only if necessary
}

