#pragma once

#include <string>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <map>
#include <set>

#include "csv_util.h"
#include "floor.h"
#include "symbol_map.h"
#include "md_snap.h"

namespace pm {

    class FloorBase {
    public:
        enum EventType {
            NOOP = 0,
            ExecutionReport = 1,
            SetPositionReq = 2,
            SetPositionAck = 3,
            GetPositionReq = 4,
            GetPositionResp = 5,
            SendOrderReq = 6,
            SendOrderAck = 7,
            UserReq = 8,
            UserResp = 9,
            ExecutionReplayReq = 10,
            ExecutionReplayAck = 11,
            ExecutionReplayDone = 12,
            ExecutionOpenOrderReq = 13,
            ExecutionOpenOrderAck = 14,
            AlgoUserCommand = 15,
            AlgoUserCommandResp = 16,
            TotalTypes 
        };

        struct PositionRequest {
            char algo[16];
            char symbol[32];
            int64_t qty_done;
            int64_t qty_open;
            PositionRequest() 
            : qty_done(0), qty_open(0) {
                algo[0] = 0;
                symbol[0] = 0;
            }
            PositionRequest(const std::string& algo_, 
                            const std::string& symbol_,
                            int64_t qty_done_, int64_t qty_open_)
            : qty_done(qty_done_), qty_open(qty_open_) {
                if ( (algo_.size() > sizeof(algo)-1) ||
                     (symbol_.size() > sizeof(symbol)-1) ){
                    logError("algo or symbol string size too long! %s, %s", 
                            algo_.c_str(), symbol_.c_str());
                    throw std::runtime_error("algo or symbol string size too long!");
                }
                std::strcpy(algo, algo_.c_str());
                std::strcpy(symbol, symbol_.c_str());
            }
        };

        struct PositionInstruction {
            char algo[16];
            char symbol[32];
            int64_t qty;
            double px; // maximum price before giving up
            int target_utc; // latest time before cancel all, in utc second
            int type; // trading style etc

            enum TYPE {
                INVALID = 0,
                MARKET = 1,
                LIMIT = 2,
                PASSIVE = 3,
                TWAP = 4,
                TRADER_WO = 5,
                TOTAL_TYPES
            };

            PositionInstruction()
            : qty(0), px(0),target_utc(0), type(-1) {
                algo[0] = 0;
                symbol[0] = 0;
            }
            PositionInstruction(const std::string& algo_,
                                const std::string& symbol_,
                                int64_t qty_desired_,
                                double px_limit_ = 0,
                                int target_utc_ = 0,
                                TYPE type_ = MARKET) 
            : qty (qty_desired_), px(px_limit_), target_utc(target_utc_), type((int)type_)
            {
                if ( (algo_.size() > sizeof(algo)-1) ||
                     (symbol_.size() > sizeof(symbol)-1) ) {
                    logError("algo or symbol string size too long! %s, %s", 
                            algo_.c_str(), symbol_.c_str());
                    throw std::runtime_error("algo or symbol string size too long!");
                }
                std::strcpy(algo, algo_.c_str());
                std::strcpy(symbol, utils::SymbolMapReader::get().getTradableSymbol(symbol_).c_str());
            }

            PositionInstruction(const PositionInstruction& pi) {
                memcpy(this, &pi, sizeof(PositionInstruction));
            }

            PositionInstruction(const char* pi_str) :
            px(0), target_utc(0) {
                // form of algo, symbol, qty [, px_str|twap_str]
                // the twap_str starts with 'T', followed by a number and a 's|m|h'
                // for example T5m
                // px_str is a md parsable string
                std::string respstr;

                type = FloorBase::PositionInstruction::MARKET;
                auto tk = utils::CSVUtil::read_line(pi_str);
                if (!(tk.size() == 3) && !(tk.size() == 4)) {
                    respstr = std::string("Set position not understood, (")  + std::string(pi_str) + std::string(") check help str!");
                    logError("%s", respstr.c_str());
                    throw std::runtime_error(respstr);
                }
                std::strcpy(algo, tk[0].c_str());
                std::strcpy(symbol, utils::SymbolMapReader::get().getTradableSymbol(tk[1]).c_str());


                qty = std::stoll(tk[2]);
                if (tk.size() == 4) {
                    // get the price from price str
                    if (tk[3][0] == 'T') {
                        // expect twap_str such as T5m
                        type = FloorBase::PositionInstruction::TWAP;
                        int dur = std::stoi(tk[3].substr(1, tk[3].size()-2));
                        char unit = tk[3][tk[3].size()-1];
                        if (unit == 'm' || unit=='M') {
                            dur *= 60;
                        } else if (unit == 'h' || unit == 'H') {
                            dur *= 3600;
                        } else if (unit == 's' || unit == 'S') {
                        } else {
                            respstr = "unknow twap unit" + tk[3];
                            logError("%s", respstr.c_str());
                            throw std::runtime_error(respstr);
                        }
                        target_utc = utils::TimeUtil::cur_utc() + dur;
                    } else {
                        // expect px_str such as a+t1
                        type = FloorBase::PositionInstruction::LIMIT;
                        if (!md::getPriceByStr(symbol, tk[3].c_str(), px)) {
                            respstr = std::string("Error getting price str: ") + tk[3];
                            logError("%s",respstr.c_str());
                            throw std::runtime_error(respstr);
                        }
                    }
                } else {
                    // a market order
                    type = FloorBase::PositionInstruction::MARKET;
                }
            }

            std::string toString() const {

                const auto& tradable = utils::SymbolMapReader::get().getTradableSymbol(symbol);
                const auto& mts_contract = utils::SymbolMapReader::get().getByTradable(tradable)->_mts_contract;

                char buf[256];
                snprintf(buf, sizeof(buf), "PositionInstruction: %s, %s, %lld, %s, %s, %s", 
                        algo, mts_contract.c_str(), (long long)qty, PriceCString(px), target_utc==0?"":utils::TimeUtil::frac_UTC_to_string(target_utc, 0).c_str(), TypeString((TYPE)type).c_str());
                return std::string(buf);
            }

            static std::string TypeString(TYPE type) {
                switch (type) {
                case INVALID: 
                    return "INVALID";
                case MARKET: 
                    return "MARKET";
                case LIMIT:
                    return "LIMIT";
                case PASSIVE:
                    return "PASSIVE";
                case TWAP:
                    return "TWAP";
                case TRADER_WO:
                    return "TraderWorkOrder";
                default :
                    return "Unknown";
                }
            }

        };

        using MsgType = utils::Floor::Message;
        using ChannelType = std::unique_ptr<utils::Floor::Channel>;

        explicit FloorBase(const std::string& name, bool is_server);
        ~FloorBase() {};

        template<typename ServerType>
        bool run_one_loop(ServerType& server); 
        // For service provider to check incoming requests
        // ServerType is expected to implement a function
        // void handleMessage(MsgType& msg_in)
        // returns true if processed a request, false if idle

        void subscribeMsgType(const std::set<int> type_set) {
            m_channel->addSubscription(type_set);
        }

        const std::string m_name;
        ChannelType m_channel;

        static std::shared_ptr<FloorBase> getFloor(const std::string& name, bool is_server, const std::string& floor_name);
        // this operates on a different floor by the name of floor_name.  Usually used as simulation
        // clients/server can only communicates on the same floor.
        // Use the public FloorBase constructor for default floor
        
    protected:

        FloorBase(const std::string& name, bool is_server, const std::string& floor_name);
        MsgType m_msgin, m_msgout;
        bool parseKeyValue (const std::string& cmd, std::map<std::string, std::string>& key_map) const;
    };

    // function definitions
    
    inline
    FloorBase::FloorBase(const std::string& name, bool is_server)
    : m_name(name), 
      m_channel(is_server? utils::Floor::get().getServer() :
                           utils::Floor::get().getClient())
    {
        logDebug("%s created as %s", name.c_str(),  (is_server?"Server":"Client"));
    };

    inline
    FloorBase::FloorBase(const std::string& name, bool is_server,  const std::string& floor_name)
    : m_name(name), 
      m_channel(is_server? utils::Floor::getByName(floor_name.c_str())->getServer() :
                           utils::Floor::getByName(floor_name.c_str())->getClient())
    {
        logDebug("%s (Name %s) created as %s",
                name.c_str(), floor_name.c_str(), (is_server?"Server":"Client"));
    };

    inline
    std::shared_ptr<FloorBase> FloorBase::getFloor(
            const std::string& name,
            bool is_server,
            const std::string& floor_name
    ) {
        return std::shared_ptr<FloorBase>( new FloorBase(name, is_server, floor_name) );
    }

    inline
    bool FloorBase::parseKeyValue (const std::string& cmd, std::map<std::string, std::string>& key_map) const {
        // parse key=val,... replace ALL to empty string
        try {
            key_map.clear();
            auto tokens = utils::CSVUtil::read_line(cmd);
            for (const auto& tk: tokens) {
                auto fields = utils::CSVUtil::read_line(tk, '=');
                if (fields.size() == 2)
                    key_map[fields[0]] = ((fields[1]=="ALL") ? "" : fields[1]);
            }
        } catch (const std::exception& e) {
            logError("problem parsing key value: %s\n%s", cmd.c_str(), e.what());
            return false;
        }
        return true;
    };

    template<typename ServerType>
    inline
    bool FloorBase::run_one_loop(ServerType& server) {
        if (m_channel->nextMessage(m_msgin)) {
            server.handleMessage(m_msgin);
            return true;
        }
        return false;
    };
};

