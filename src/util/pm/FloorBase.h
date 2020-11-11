#pragma once

#include "floor.h"
#include <string>
#include <map>
#include "csv_util.h"
#include <memory>

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
            SendOrderResp = 7,
            UserReq = 8,
            UserResp = 9,
            ExecutionReplayReq = 10,
            ExecutionReplayAck = 11,
            ExecutionReplayDone = 12,
            ExecutionOpenOrderReq = 13,
            ExecutionOpenOrderAck = 14,
            TotalTypes = 15
        };

        explicit FloorBase(const std::string& name, bool is_server);
        ~FloorBase() {};

        template<typename ServerType>
        bool run_one_loop(ServerType& server); 
        // For service provider to check incoming requests
        // ServerType is expected to implement a function
        // void handleMessage(MsgType& msg_in)
        // returns true if processed a request, false if idle

        using MsgType = utils::Floor::Message;
        using ChannelType = std::unique_ptr<utils::Floor::Channel>;
        const std::string m_name;

    protected:
        ChannelType m_channel;
        MsgType m_msgin, m_msgout;
        bool parseKeyValue (const std::string& cmd, std::map<std::string, std::string>& key_map) const;
    };

    // function definitions
    
    inline
    FloorBase::FloorBase(const std::string& name, bool is_server)
    : m_name(name), 
      m_channel(is_server? utils::Floor::get().getServer() :
                           utils::Floor::get().getClient())
    {};

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
            fprintf(stderr, "problem parsing key value: %s\n%s\n", cmd.c_str(), e.what());
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

