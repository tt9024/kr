#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <stdexcept>
#include "plcc/Logger.hpp"
#include "plcc/ConfigureReader.hpp"
#include <map>

static const char* LoggerConfigKey = "Logger";
static const char* ConfigFilePath =  "config/main.cfg";

#define logDebug(a...)
#define logInfo(a...) utils::PLCC::instance().logInfo(__FILE__,__LINE__,a)
#define logError(a...) utils::PLCC::instance().logError(__FILE__,__LINE__,a)

#define plcc_getInt(a)       utils::PLCC::instance().get<int>(a)
#define plcc_getLongLong(a)  utils::PLCC::instance().get<long long>(a)
#define plcc_getDouble(a)    utils::PLCC::instance().get<double>(a)
#define plcc_getString(a)    utils::PLCC::instance().get<std::string>(a)
#define plcc_getStringArr(a) utils::PLCC::instance().getArr<std::string>(a)

namespace utils {


class PLCC : public ConfigureReader, public FileLogger {
public:
    static const char* getConfigPath() {
        return ConfigFilePath;
    };
    static const std::string getLogFileName(std::string logfile,
            std::string instname) 
    {
         return logfile+
                (instname.size()>0?(std::string("_")+instname):std::string(""))+
                std::string("_")+ 
                TimeUtil::frac_UTC_to_string(0,0,"%Y%m%d") +
                std::string(".txt");
    }

    static void setConfigPath(const char* cfg_path) {
        ConfigFilePath = cfg_path;
    }

    static PLCC& instance(const char* instname=NULL) {
        // note this is NOT thread safe
        static std::map<std::string, PLCC*>plcc_map;
        static PLCC* default_plcc=NULL;

        const std::string inststr=std::string(instname?instname:"");
        if (!default_plcc) {
            default_plcc=new PLCC(getConfigPath(),inststr);
            plcc_map[inststr] = default_plcc;
            return *default_plcc;
        }

        if (inststr.size()==0) {
                return *default_plcc;
        }
        auto iter=plcc_map.find(inststr);
        if (iter !=plcc_map.end()) {
                return *(iter->second);
        }
        PLCC* inst=new PLCC(getConfigPath(), inststr);
        plcc_map[inststr]=inst;
        return *inst;
    }

private:

    explicit PLCC(const char* configFileName, const std::string& instname) :
        ConfigureReader(configFileName),
        FileLogger(getLogFileName(get<std::string>(LoggerConfigKey),instname).c_str()),
        m_configFileName(configFileName)
    {}
    ~PLCC() {
    };


    std::string m_configFileName;
};
}
