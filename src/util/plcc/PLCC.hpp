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
#include <map>

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
#define plcc_getStringArr(a...) utils::PLCC::instance().getStringArr(a)

namespace utils {


class PLCC : public ConfigureReader, public FileLogger {
public:
    static const char* getConfigPath() {
        // TODO - read from the shell as well
        return ConfigFilePath;
    };
    static const std::string getLogFileName(std::string logfile,
    		                                std::string instname) {
    	 return logfile+
    	        (instname.size()>0?(std::string("_")+instname):std::string(""))+
    	        std::string("_")+
				TimeUtil::cur_time_to_string_day() +
    			std::string(".txt");
    }

    static PLCC& instance(const char* instname=NULL) {
        static std::map<std::string, PLCC*>plcc_map;
        static PLCC* default_plcc=NULL;

        // not thread safe
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
        FileLogger(getLogFileName(getString(LoggerConfigKey),instname).c_str()),
        m_configFileName(configFileName)
    {}
    ~PLCC() {
    };


    std::string m_configFileName;
};
}
