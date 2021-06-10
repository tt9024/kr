#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <stdexcept>
#include "plcc/Logger.hpp"
#include "plcc/ConfigureReader.hpp"
#include "csv_util.h"

#include <map>
#include <string.h>

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/')+1 : __FILE__)

#define logDebug(a...)
#define logInfo(a...) utils::PLCC::instance().logInfo(__FILENAME__,__LINE__,a)
#define logError(a...) utils::PLCC::instance().logError(__FILENAME__,__LINE__,a)

#define plcc_getInt(a...) utils::PLCC::instance().get<int>(a)
#define plcc_getDouble(a...) utils::PLCC::instance().get<double>(a)
#define plcc_getString(a...) utils::PLCC::instance().get<std::string>(a)
#define plcc_getStringArr(a...) utils::PLCC::instance().getArr<std::string>(a)

#define PRICE_PRECISION 8
#define PriceString(px)  utils::CSVUtil::printDouble((px),  PRICE_PRECISION)
#define PriceCString(px) utils::CSVUtil::printDouble((px),  PRICE_PRECISION).c_str()

#define PNL_PRECISION 2
#define PnlString(pnl)   utils::CSVUtil::printDouble((pnl), PNL_PRECISION)
#define PnlCString(pnl)  utils::CSVUtil::printDouble((pnl), PNL_PRECISION).c_str()

#define DefaultLoggerConfigKey "Logger"
#define DefaultConfigFilePath  "config/main.cfg"

namespace utils {

class PLCC : public ConfigureReader, public FileLogger {
public:
    static const char* LoggerConfigKey;
    static const char* ConfigFilePath;

    static const char* getConfigPath();
    static void setConfigPath(const char* cfg_path);
    static const std::string getLogFileName(std::string logfile, std::string instname);
    static PLCC& instance(const char* instname=NULL);

private:
    explicit PLCC(const char* configFileName, const std::string& instname);
    ~PLCC();
    std::string m_configFileName;
};
}
