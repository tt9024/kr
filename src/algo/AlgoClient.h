#pragma once

#include "ExecutionReport.h"
#include "FloorBase.h"
#include "csv_util.h"
#include <stdio.h>

namespace algo {

    class FloorClientAlgo: public FloorBase {
        // FloorClientAlgo act as an interface between 
        // Floor Manager and the Algo
        // for sending orders and receiving execution reports. 
        //
        // Templated type AlgoThread needs to implement :
        // 1. errstr = handleUserCommand(char* data, int size)
        // In addition, it needs to update this client with incoming :
        // 2. sendMsg(msg)

    public:
        explicit FloorClientAlgo(const std::string& name) 
        : FloorBase(name, false)
        { 
            setSubscriptions();
        };

        ~FloorClientAlgo(){};

        // update the floor manager with a composed message
        void sendMsg(const FloorBase::MsgType& msg) {
            m_channel->update(msg);
        }

        // get next subscribed message (user command)
        // and call this->handleMessage() on the message.
        bool checkFloorMessage() {
            return this->run_one_loop(*this);
            // this will call this->handleMessage() with the received msg
        }

        // out-going updates
        bool sendPositionRequest(const FloorBase::PositionRequest&& pr) {
            static FloorBase::MsgType msgreq(FloorBase::GetPositionReq, nullptr, 0, 0), msgresp;
            msgreq.copyData((const char*)&pr, sizeof(FloorBase::PositionRequest));
            return m_channel->request(msgreq, msgresp);
        }

        bool sendPositionInstructure(const FloorBase::PositionInstruction&& pi) {
            static FloorBase::MsgType msgreq(FloorBase::SetPositionReq, nullptr, 0, 0), msgresp;
            msgreq.copyData((const char*)&pi, sizeof(FloorBase::PositionInstruction));
            return m_channel->requestAndCheckAck(msgreq, msgresp, 1, FloorBase::SetPositionAck);
        }


        template<typename AlgoType>
        void handleMessage(const MsgType& msg_in, AlgoType& conn) {
            // the algo command is expected to be
            // 'algo S' start algo
            // 'algo R config_file' reload algo with config_file
            // 'algo E' stop algo
            // 'algo D' dump algo status
            // 'algo T utc_second' triger algo run at utc_second
            switch (msg_in.type) {
                case FloorBase::AlgoUserCommand :
                {
                    /*
                    const char* cmd = msg_in.buf;
                    auto tk = utils::CSVUtil::read_line(cmd);
                    if (strcmp(tk[0], m_co
                    switch (cmd[0]) {
                    case 'D':
                    }
                    */

                    const std::string errstr = conn.handleUserCommand(msg_in.buf, msg_in.data_size);
                    m_channel->updateAck(msg_in, m_msgout, FloorBase::AlgoUserCommandResp, errstr);

                    break;
                }
                default :
                    fprintf(stderr, "unkown requests %s\n", msg_in.toString().c_str());
            }
        }

    private:
        void setSubscriptions() {
            std::set<int> type_set;
            type_set.insert((int)FloorBase::AlgoUserCommand);
            m_channel->addSubscription(type_set);
        }
        friend class FloorBase;
    };
};

