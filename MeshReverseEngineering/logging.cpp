//
// Created by victoryang00 on 1/13/23.
//

#include "logging.h"
#include <iostream>

void LogWriter::operator<(const LogStream &stream) {
    std::ostringstream msg;
    if (log_level_ == TRACE)
        file_ << stream.sstream_->rdbuf();
    else {
        msg << stream.sstream_->rdbuf();
        output_log(msg);
    }
}

void LogWriter::output_log(const std::ostringstream &msg) {
    if (log_level_ >= env_log_level)
        std::cout << std::format( "{} ", msg.str());
}
std::string level2string(LogLevel level) {
    switch (level) {
    case DEBUG:
        return "DEBUG";
    case INFO:
        return "INFO";
    case WARNING:
        return "WARNING";
    case ERROR:
        return "ERROR";
    default:
        return "";
    }
}
