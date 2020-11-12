#include "ExecutionReport.h"
#include "TPClient.h"
#include "time_util.h"
#include <map>
#include <memory>
#include <vector>
#include <stdexcept>
#include <csignal>

namespace pm {

    template<typename Timer>
    class OrderConnectionMock {
    public:
        OrderConnectionMock(const std::string& name, std::string existing_er_file)
        : _name(name), 
          _cur_micro( Timer::cur_micro() ),
          _cur_id(100),
          _should_run(false),
          _floor(name, *this)
        {
            if (existing_er_file.size() > 0) {
                populate_history_fill(existing_er_file);
            }
        };

        void run() {
            _should_run = true;
            while (_should_run) {
                _cur_micro = Timer::cur_micro();
                const auto& iter = _to_publish.begin();
                if (iter != _to_publish.end() && iter->first < _cur_micro) {
                    // publish the next message
                    // if it's a fill, append to history_fill history
                    auto& msg = iter->second;
                    _floor.sendMsg(*msg);
                    if (msg->type == FloorBase::ExecutionReport) {
                        ExecutionReport* er = (ExecutionReport*)(msg->buf);
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

        void stop() {
            fprintf(stderr, "%s stopping...\n", _name.c_str());
            _should_run = false; 
        };

        std::string sendOrder(char* data, int size) {
            // order string is supposed to be the following
            // B/S algo, symbol, qty, px
            try {
                int bs = (data[0]=='B'?1:-1);
                auto line = utils::CSVUtil::read_line(std::string(data+1));

                // new scheduled in 0.1 seconds
                auto new_micro = _cur_micro + 100000;
                ExecutionReport er_new ( line[0], line[1], 
                        "cid"+std::to_string(_cur_id), "exid"+std::to_string(_cur_id),
                        "0",  // tag39 = new
                        std::stoi(line[2])*bs,
                        std::stod(line[3]),
                        utils::TimeUtil::frac_UTC_to_string(new_micro/1000ULL, 3),
                        "",
                        new_micro);
                enq(er_new);

                // fill scheduled in 0.5 second
                auto fill_micro = _cur_micro + 500000;
                ExecutionReport er_fill ( line[0], line[1], 
                        "cid"+std::to_string(_cur_id), "exid"+std::to_string(_cur_id),
                        "2",  // tag39 = fill 
                        std::stoi(line[2])*bs,
                        std::stod(line[3]),
                        utils::TimeUtil::frac_UTC_to_string(fill_micro/1000ULL, 3),
                        "",
                        fill_micro);
                enq(er_fill);
            } catch (const std::exception& e) {
                return e.what();
            }
            return "";
        }

        std::string requestReplay(char* data, int size) {
            // write all the existing ers in the history vector into the given file
            // schedule a message of done in 2 seconds
            // return errstr
            try {
                utils::CSVUtil::FileTokens lines;
                // _history_fill is maintained as all past fills published
                for (const auto& er: _history_fill) {
                    lines.push_back(((ExecutionReport*)(er->buf))->toCSVLine());
                }
                std::string fn(data);
                utils::CSVUtil::write_file(lines, fn, false);
                _to_publish.emplace( _cur_micro + 2000000,  
                                     std::make_shared<FloorBase::MsgType>(
                                         FloorBase::ExecutionReplayDone, 
                                         fn.c_str(), 
                                         fn.size()+1)
                                   );
            } catch (const std::exception& e) {
                return e.what();
            }
            return "";
        }

        std::string requestOpenOrder(char* data, int size) {
            // schedule a message of one open order in 0.1 second
            // the open order will never be filled
            // return errstr

            static std::string line = "sym1, algo1,cid1, eid1, 0,-10, 3.0, 20201004-18:33:02,, 1601850782023138";
            try {
                ExecutionReport er_oo = ExecutionReport::fromCSVLine(utils::CSVUtil::read_line(line));
                er_oo.m_recv_micro=_cur_micro+1000000;
                enq(er_oo);
            } catch (const std::exception& e) {
                return e.what();
            }
            return "";
        }

        std::string toString() const {
            char buf[256];
            size_t bytes = snprintf(buf, sizeof(buf), "%s: cur_micro: %s, hist_queue_size: %d, pub_queue_size: %d\n", _name.c_str(), utils::TimeUtil::frac_UTC_to_string(_cur_micro, 6).c_str(), (int)_history_fill.size(), (int)_to_publish.size());
            return std::string(buf);
        }

    private:
        const std::string _name;
        std::map<uint64_t, std::shared_ptr<FloorBase::MsgType> > _to_publish;
        std::vector<std::shared_ptr<FloorBase::MsgType> > _history_fill;
        uint64_t _cur_micro;
        uint64_t _cur_id;
        volatile bool _should_run;
        FloorClientOrder<OrderConnectionMock<Timer> > _floor;

        void populate_history_fill(const std::string& csv_file) {
            auto er_lines = utils::CSVUtil::read_file(csv_file);
            for (const auto& line: er_lines) {
                const auto er = ExecutionReport::fromCSVLine(line);
                if (er.isFill()) {
                    _history_fill.emplace_back(
                            std::make_shared<FloorBase::MsgType>(
                                FloorBase::ExecutionReport,
                                (const char*)&er,
                                sizeof(ExecutionReport)));
                }
            }
        }

        void enq(const ExecutionReport& er) {
            _to_publish.emplace(er.m_recv_micro, 
                                std::make_shared<FloorBase::MsgType> (
                                    FloorBase::ExecutionReport,
                                    (const char*)&er,
                                    sizeof(ExecutionReport))
                               );
        }
    };
};

std::shared_ptr<pm::OrderConnectionMock<utils::TimeUtil> > ocm;

void signal_handler(int signal)
{
    ocm->stop();
    std::atomic_signal_fence(std::memory_order_release);
    fprintf(stderr, "received signal %d, floor manager stop() called...\ndump state\n%s\n",
            signal, ocm->toString().c_str());
}

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::string previou_fill_file = "";
    std::string previous_fill_file;
    if (argc > 1) {
        previous_fill_file = std::string(argv[1]);
    }
    ocm = std::make_shared<pm::OrderConnectionMock<utils::TimeUtil> >("mock_exchange", previous_fill_file);
    ocm->run();
    return 0;
}

