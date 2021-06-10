#include "plcc/PLCC.hpp"

namespace utils {

const char* PLCC::LoggerConfigKey = DefaultLoggerConfigKey;
const char* PLCC::ConfigFilePath =  DefaultConfigFilePath;

const char* PLCC::getConfigPath() {
    return ConfigFilePath;
};

void PLCC::setConfigPath(const char* cfg_path) {
    ConfigFilePath = cfg_path;
}

const std::string PLCC::getLogFileName(std::string logfile,
        std::string instname) 
{
    // use the trading day instead of calendar day
    const auto trading_ymd = TimeUtil::tradingDay(0, -6, 0, 17, 30, 0, 2);

    return logfile+
            (instname.size()>0?(std::string("_")+instname):std::string(""))+
            std::string("_")+trading_ymd+
            std::string(".txt");
}

PLCC& PLCC::instance(const char* instname) {
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

PLCC::PLCC(const char* configFileName, const std::string& instname) :
    ConfigureReader(configFileName),
    FileLogger(getLogFileName(get<std::string>(LoggerConfigKey, nullptr, "stdout"),instname).c_str()),
    m_configFileName(configFileName)
{}

PLCC::~PLCC() {}
}
