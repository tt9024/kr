#include "FloorBase.h"
#include "time_util.h"
#include <stdio.h>
#include <iostream>
#include <istream>

namespace pm {
    class FloorClientUser: public FloorBase {
        // FloorClientUser encapsulates user input, sends to 
        // FloorManager and output the response.
        // 

    public:
        explicit FloorClientUser(const std::string& name)
        : FloorBase(name, false)
        {}

        ~FloorClientUser(){};

        // update the floor manager whenever
        std::string sendReq(const std::string& cmd) {
            switch (cmd[0]) {
            case '@':
            {
                // FloorBase::AlgoUserCommand
                m_msgin.type = FloorBase::AlgoUserCommand;
                m_msgin.copyString(cmd.c_str()+1);
                if ((m_channel->request(m_msgin, m_msgout, 5) && 
                    (m_msgout.type == FloorBase::AlgoUserCommandResp))) {
                    return std::string(m_msgout.buf);
                } else {
                    return "Reqeust Timedout!";
                }
            }
            default:
            {
                // FloorBase::UserReq
                m_msgin.type = FloorBase::UserReq;
                m_msgin.copyString(cmd);
                if ((m_channel->request(m_msgin, m_msgout, 5) && 
                    (m_msgout.type == FloorBase::UserResp))) {
                    return std::string(m_msgout.buf);
                } else {
                    return "Reqeust Timedout!";
                }
            }
            }
        }

    protected:
        FloorClientUser(const FloorClientUser& mgr) = delete;
        FloorClientUser& operator=(const FloorClientUser& mgr) = delete;
    };
};

static const char* Version = "1.0";

int main(int argc, char**argv) {
    pm::FloorClientUser fcu ("user");
    std::cout << "Floor Client Version "<< Version << std::endl;
    while (true) {
        std::cout << ">>> ";
        std::string cmd;
        std::getline (std::cin >> std::ws ,cmd);
        std::cout << fcu.sendReq(cmd) << std::endl;
    }
    return 0;
}
