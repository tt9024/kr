#pragma once

#include "FloorBase.h"
#include "ExecutionReport.h"
#include "csv_utils.h"
#include <map>
#include <memory>
#include <vector>

namespace pm {

    class OrderConnectionMock {
    public:
        OrderConnectionMock(std::string existing_er_file) {
            // load the er into a vector
            // initilize the time_idx, the running_id
        };

        std::string sendOrder(char* data, int size) {
            // schedule a message of open and fill within .1 and .5 seconds
            // return errstr
        }

        std::string requestReplay(char* data, int size) {
            // write all the existing ers in the history vector into the given file
            // schedule a message of done in 2 seconds
            // return errstr
        }

        std::string requestOpenOrder(char* data, int size) {
            // schedule a message of one open order in 0.1 second
            // the open order will never be filled
            // return errstr
        }

    private:
        FloorClientOrder<OrderConnectionMock> _floor_client;
        std::map<uint64_t, std::shared_ptr<FloorBase::MsgType> > _er_current;
        std::vector<std::shared_ptr<FloorBase::MsgType> > _er_history;
    }

}


int main(int argc, char** argv) {
    std::string previou_fill_file = "";
    if (argc > 1) {
        previous_fill_file = std::string(argv[1]);
    }


}

