#include "queue.h"
#include <memory>

namespace utils {

    // Event channel that handles message passing between
    // clients: i.e. algo, tp
    // servers: i.e. traders

    class Floor {
    public:
        enum EventType {
            NOOP = 0,
            ExecutionReport = 1,
            SetPositionReq = 2,
            SetPositionResp = 3,
            GetPositionReq = 4,
            GetPositionResp = 5,
            SendOrderReq = 6,
            SendOrderResp = 7,
            UserReq = 8,
            UserResp = 9,
            TotalTypes = 10
        };

        struct Message {
            // a stateless message that doesn't demand response
            EventType type;
            uint64_t ref;
            char* buf;
            size_t data_size;
            size_t buf_capacity;

            //uint64_t id;
            Message() : type(NOOP), ref((uint64_t)-1), buf(nullptr), data_size(0), buf_capacity(0) {}

            Message(EventType type_, char* data_, size_t size_, uint64_t ref_=(uint64_t)-1)
            : type(type_), ref(ref_), buf((char*)malloc(size_)), data_size(size_), buf_capacity(size_) 
            {
                memcpy(buf, data_, size_);
            }
            ~Message() {
                if (buf && buf_capacity) {
                    buf_capacity=0;
                    free (buf);
                    buf=nullptr;
                }
            }

            void copyData(const char* data, const size_t data_size_) {
                if (buf_capacity < data_size_) {
                    if (buf) {
                        free (buf);
                    }
                    buf = (char*)malloc(data_size_);
                    buf_capacity = data_size_;
                }
                memcpy(buf, data, data_size);
                data_size = data_size_;
            }

            std::string toString() const {
                char strbuf[128];
                snprintf(strbuf, sizeof(strbuf), 
                        "Floor Message (type: %d, ref: %llu, buf: %p, data_size: %d, buf_capacity: %d)",
                        (int) type, (unsigned long long) ref, buf, (int) data_size, (int) buf_capacity);
                return std::string(strbuf);
            }
        }

        using QType = utils::MwQueue<QLen, utils::ShmCircularBuffer>;

        static Floor& get() {
            static Floor flr;
            return flr;
        };

        ~Floor();
        
        std::unique_ptr<Channel> getClient() const {
            // create a channel for write_qin, read_qout
            return std::make_shared<Channel>(_qout, _qin);
        }

        std::unique_ptr<Floor::Channel> getServer() const {
            // create a channel for read_qin, write_qout
            return std::make_shared<Channel>(_qin, _qout);
        }

    private:
        static const char * q1_name = "fq1";
        static const char * q2_name = "fq2";
        static const int QLen = 1024*1024*64; // 64M

        std::shared_ptr<QType> _qin, _qout;

        Floor()
        : _qin (std::make_shared<QType>(q1_name, false, false)), 
          _qout(std::make_shared<QType>(q2_name, false, false))
        {
            // createt the two queues read+write, without init to zero
        }
        Floor(Const Floor&) = delete ;
        void operator=(Const Floor&) = delete;

    public:
        class Channel {
        public:

            Channel(std::shared_ptr<QType> qread, std::shared_ptr<QType> qwrite)
            : _qin(qread), _writer(std::make_shared<QType::Writer>(*qwrite)) {
            }

            /* For Floor Clients
             */
            bool request(const Message& req, Message& resp, int timeout_sec=1) {
                // request encode the type, msg and size
                // resp will be byte-wise copied from the queue, with resp_size set accordingly. 
                // timeout_sec is the longest time to wait after when it returns false.
                // Memory allocation wise, caller is responsible to allocate enough memory in the
                // resp message and set the size as available. Upon return, the resp byte-wise
                // copied and size updated. It returns false if size is not enough.
                return sendSync(req, resp, timeout_sec);
            }

            bool update(const Message& upd) {
                // send a update and return
                return sendMessage(upd.type, upd.data, upd.size);
            }

            /* for the channel server
             */
            bool nextMessage(Message* msg, std::shared_ptr<QType::Reader> reader, bool filter_on) {
                    char* buf;
                    int bytes;
                    QStatus status = reader->takeNextPtr(buf, bytes);
                    if (status == utils::QStat_OK) {
                        reader->advance(bytes);
                        msg->type = readMessage(buf, bytes, &(msg->ref));
                        if ( (!filter_on) ||
                             (_subscribed_types.find(msg->type) != _subscribed_types.end()) ) {
                            msg->copyData(buf, bytes);
                            return true;
                        }
                    } else {
                        if (status != utils::QStat_EAGAIN) {
                            // overflow or error, sync
                            fprintf(stderr, "sendSync got read error from reader queue. %d, %s\n", (int)status, reader->dump_state().c_str());
                            reader->syncPos();
                        }
                    }
                    return false;
            }

            void addSubscription(const std::set<EventType>& type_set) {
                for (auto tp : type_set) {
                    _subscribed_types.insert(tp);
                }
            }
            
            void removeSubscription(const std::set<EventType>& type_set) {
                for (auto tp : type_set) {
                    _subscribed_types.erase(tp);
                }
            }

        private:
            std::shared_ptr<QType> _qin, _qout;
            std::shared_ptr<Qtype::Writer> _writer;
            std::set<int> _subscribed_types;

            bool sendSync(const Message& req,  Message* resp, int timeout_sec) {
                // create a new reader (position sync'ed), send the request, record the sending 
                // position as reference, scan through the read queue for a message matching with referece
                // return true if found within timeout, otherwise, false
                
                std::shared_ptr<QType::Reader> reader(_qin->newReader());  //this sync the read position to latest
                utils::QPos pos_ref = sendMessage(req.type, req.data, req.data_size);
                uint64_t timeout_micro = utils::TimeUtil::cur_micro() + (uint64_t)timeout_sec*1000000ULL;
                while (utils::TimeUtil::cur_micro() < timeout_micro) {
                    if ( nextMessage(resp, reader, false) ) {
                        if (resp->ref == pos_ref) {
                            return true;
                        }
                        continue;
                    }
                    utils::TimeUtil::micro_sleep(10 * 1000);
                }
                fprintf(stderr, "sendSync timeout while waiting for response. event_type: %d\n", et);
                return false;
            }

            utils::QPos sendMessage(int et, char* msg_data, size_t msg_size, uint64_t ref= (uint64_t)-1) {
                // header has format of type(int), ref(uint64_t)
                static const int hdrsize = sizeof(int) + sizeof(uint64_t);
                char buf[hdrsize];
                memcpy(buf, &et, sizeof(int));
                memcpy(buf+sizeof(int), &ref, sizeof(uint64_t));
                return _writer->put(buf, hdrsize, msg_data, msg_size);
            }

            int readMessage(char*& buf, int* bytes, uint64_t* ref) {
                // given a raw message read from the queue as buf and bytes
                // parse the type and ref and adjust the buf and bytes for payload data
                
                int type = *(int*)buf;
                buf+=sizeof(int);
                (*bytes)-=sizeof(int);
                *ref = *(uint64_t*)buf;
                buf+=sizeof(uint64_t);
                (*bytes)-=sizeof(uint64_t);
                return type;
            }
        }
    }
}

