/*
 * PLCC.hpp
 *
 *  Created on: May 26, 2014
 *      Author: zfu
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <stdexcept>
#include "plcc/Logger.hpp"
#include "plcc/ConfigureReader.hpp"

static const char* LoggerConfigKey = "logger";
static const char* ConfigFilePath =  "config/main.cfg";

// TODO: include file and line number
//#define logDebug(a...) utils::PLCC::instance().logDebug(a)
#define logDebug(a...) utils::PLCC::instance().logDebug(__FILE__,__LINE__,a)
#define logInfo(a...) utils::PLCC::instance().logInfo(__FILE__,__LINE__,a)
#define logError(a...) utils::PLCC::instance().logError(__FILE__,__LINE__,a)

#define plcc_getInt(a...) utils::PLCC::instance().getInt(a)
#define plcc_getDouble(a...) utils::PLCC::instance().getDouble(a)
#define plcc_getString(a...) utils::PLCC::instance().getString(a)
#define plcc_getStringArr(a...) utils::PLCC::instance().getStrigArr(a)

namespace utils {


class PLCC : public ConfigureReader, public FileLogger {
public:
    static const char* getConfigPath() {
        // TODO - read from the shell as well
        return ConfigFilePath;
    };

    static PLCC& instance() {
        static PLCC plcc(PLCC::getConfigPath());
        return plcc;
    }

private:
    PLCC(const char* configFileName) :
        ConfigureReader(configFileName),
        FileLogger(getString(LoggerConfigKey).c_str()),
        m_configFileName(configFileName)
    {}
    ~PLCC() {};


    std::string m_configFileName;
};
}
