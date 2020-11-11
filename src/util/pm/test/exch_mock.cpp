#pragma once

#include "FloorBase.h"
#include "ExecutionReport.h"
#include "csv_utils.h"
#include <map>
#include <memory>
#include <vector>

// TODO- signal

namespace pm {

    static volatile bool _should_run = false;

    template<typename Timeer>
    class OrderConnectionMock {
    public:
        OrderConnectionMock(const std::string& name, std::string existing_er_file)
        : _name(name), 
          _cur_micro( Timer::cur_micro() ),
          _cur_id(100)
          _floor(name, *this),
        {
            if (existing_er_file.size() > ) {
                populate_history_fill(existing_er_file);
            }
        };

        void run() {
            pm::_should_run = true;
            while (pm::_should_run) {
                _cur_micro = Timer::cur_micro();
                const auto& iter = _to_publish.begin();
                if (iter != _to_publish.end() && iter->first < _cur_micro) {
                    // publish the next message
                    // if it's a fill, append to history_fill history
                    auto& msg = iter->second;
                    _floor.sendMsg(*msg);
                    if (msg->type == FloorBase::ExecutionReport) {
                        pm::ExecutionReport* er = (pm::ExecutionReport*)(msg->buf);
                        if (er->isFill()) {
                            _history_fill.push_back(msg);
                        }
                    }
                    _to_publish.erase(iter);
                    continue;
                }
                if (!_floor.run_one_loop(_floor)) // this populates the queueu
                {
                    Timer::micro_sleep(100000);
                }
            }
        }

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
        const std::string _name;
        std::map<uint64_t, std::shared_ptr<FloorBase::MsgType> > _to_publish;
        std::vector<std::shared_ptr<FloorBase::MsgType> > _history_fill;
        std::vector<std::shared_ptr<FloorBase::MsgType> > _history_oo; // this never updates
        uint64_t _cur_micro;
        uint64_t _cur_id;
        FloorClientOrder<OrderConnectionMock> _floor;

        void populate_history_fill(const std::string& csv_file) {
            auto er_lines = utils::CSVUtil::read_file(existing_er_file);
            for (const auto& line: er_lines) {
                pm::ExecutionReport er(line);
                _history_fill.emplace_back(
                        std::make_shared<FloorBase::MsgType>(
                            FloorBase::ExecutionReport,
                            (char*)&er,
                            sizeof(ExecutionReport)));
            }
        }

    }

}


int main(int argc, char** argv) {
    std::string previou_fill_file = "";
    if (argc > 1) {
        previous_fill_file = std::string(argv[1]);
    }
    OrderConnectionMock ocm(previous_fill_file);
    ocm.run();
    return 0;
}

