#include "queue.h"
#include <memory>
#include <set>
#include <iostream>
#include <stdio.h>
#include "time_util.h"

namespace utils {

    // Event channel that handles message passing between
    // clients: i.e. algo, tp
    // servers: i.e. traders

    class Floor {
    public:
        // TODO - maybe protect the buf/buf_capacity
        // as private if needed in the future usage cases
        struct Message {
            // a stateless message that doesn't demand response
            int type;
            uint64_t ref;
            char* buf;
            size_t data_size;
            size_t buf_capacity;

            //uint64_t id;
            Message() : type(0), ref(NOREF), buf(nullptr), data_size(0), buf_capacity(0) {}

            Message(int type_, char* data_, size_t size_, uint64_t ref_=NOREF)
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
                reserve(data_size_);
                memcpy(buf, data, data_size);
                data_size = data_size_;
            }

            void copyString(const std::string& str) {
                copyData(str.c_str(), str.size()+1);
            }

            std::string toString() const {
                char strbuf[128];
                snprintf(strbuf, sizeof(strbuf), 
                        "Floor Message (type: %d, ref: %llu, buf: %p, data_size: %d, buf_capacity: %d)",
                        (int) type, (unsigned long long) ref, buf, (int) data_size, (int) buf_capacity);
                return std::string(strbuf);
            }

            bool refSet() const {
                return ref != NOREF;
            }

            void reserve(size_t data_size_) {
                if (buf_capacity<data_size_) {
                    if (buf) {
                        free (buf);
                    }
                    buf = (char*)malloc(data_size_*2);
                    buf_capacity = data_size_*2;
                }
            };

            static const uint64_t NOREF = static_cast<uint64_t>(-1);
        };

        static Floor& get() {
            static Floor flr;
            return flr;
        };
        ~Floor();

        class Channel;
        std::unique_ptr<Channel> getClient() const {
            // create a channel for write_qin, read_qout
            return std::unique_ptr<Channel>(new Channel(_qout, _qin));
        }

        std::unique_ptr<Channel> getServer() const {
            // create a channel for read_qin, write_qout
            return std::unique_ptr<Channel>(new Channel(_qin, _qout));
        }

    private:
        static const int QLen = 1024*1024*64; // 64M

        using QType = utils::MwQueue<QLen, utils::ShmCircularBuffer>;
        std::shared_ptr<QType> _qin, _qout;

        Floor()
        : _qin (std::make_shared<QType>("fq1", false, false)), 
          _qout(std::make_shared<QType>("fq2", false, false))
        {
            // createt the two queues read+write, without init to zero
        };

        Floor(const Floor&) = delete ;
        void operator=(const Floor&) = delete;

    public:
        class Channel {
        public:

            Channel(std::shared_ptr<QType> qread, std::shared_ptr<QType> qwrite)
            : _qin(qread), _qout(qwrite),
              _reader(std::make_shared<QType::Reader>(*_qin)),
              _writer(std::make_shared<QType::Writer>(*_qout))
            {}

            bool request(const Message& req, Message& resp, int timeout_sec=1) {
                // request encode the type, msg and size
                // resp will be byte-wise copied from the queue, with resp_size set accordingly. 
                // timeout_sec is the longest time to wait after when it returns false.
                // Memory allocation wise, caller is responsible to allocate enough memory in the
                // resp message and set the size as available. Upon return, the resp byte-wise
                // copied and size updated. It returns false if size is not enough.
                return sendSync(req, &resp, timeout_sec);
            }

            uint64_t update(const Message& upd) {
                // send a update and return a reference number
                return sendMessage(upd.type, upd.buf, upd.data_size, upd.ref);
            }

            // this reads the next message from the read queue using the 
            // channel's reader
            bool nextMessage(Message& msg) {
                return nextMessage(&msg, _reader, true);
            }

            void addSubscription(const std::set<int>& type_set) {
                for (auto tp : type_set) {
                    _subscribed_types.insert(tp);
                }
            }

            void removeSubscription(const std::set<int>& type_set) {
                for (auto tp : type_set) {
                    _subscribed_types.erase(tp);
                }
            }

            // sending request that expects an acknowledgement (Ack)
            bool requestAndCheckAck(const Message& req, Message& resp, int timeout_sec, int ack_type) {
                if (! request(req, resp, timeout_sec)) {
                    fprintf(stderr, "Error sending request: %s\n", req.toString().c_str());
                    return false;
                }
                if ( (resp.type != ack_type) || (strncmp(resp.buf, "Ack",3)!=0)) {
                    fprintf(stderr, "Received message without Ack. type=%d, msg=%s\n", 
                            (int) ack_type, resp.buf);
                    return false;
                }
                return true;
            }

            // sending acknowledgement for a request
            void updateAck(const Message& req, Message& resp, int ack_type) {
                resp.type = ack_type;
                resp.ref = req.ref;
                resp.copyString("Ack");
                update(resp);
            }

        private:
            std::shared_ptr<QType> _qin, _qout;
            std::shared_ptr<QType::Reader> _reader;
            std::shared_ptr<QType::Writer> _writer;
            std::set<int> _subscribed_types;

            bool sendSync(const Message& req,  Message* resp, int timeout_sec) {
                // create a new reader (position sync'ed), send the request, record the sending 
                // position as reference, scan through the read queue for a message matching with referece
                // return true if found within timeout, otherwise, false
                
                std::shared_ptr<QType::Reader> reader(_qin->newReader());  //this sync the read position to latest
                utils::QPos pos_ref = update(req);
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
                fprintf(stderr, "sendSync timeout while waiting for response. event_type: %d\n", (int) req.type);
                return false;
            }

            utils::QPos sendMessage(int et, char* msg_data, size_t msg_size, uint64_t ref= utils::Floor::Message::NOREF) {
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

            bool nextMessage(Message* msg, std::shared_ptr<QType::Reader> reader, bool filter_on) {
                char* buf;
                int bytes;
                while (true) {
                    QStatus status = reader->takeNextPtr(buf, bytes);
                    if (status == utils::QStat_OK) {
                        msg->type = readMessage(buf, &bytes, &(msg->ref));
                        if (! msg->refSet()) {
                            msg->ref = reader->getReadPos();
                        }
                        asm volatile("" ::: "memory");
                        reader->advance(bytes);
                        if ( (!filter_on) ||
                             (_subscribed_types.find(msg->type) != _subscribed_types.end()) ) {
                            msg->copyData(buf, bytes);
                            return true;
                        }
                        // try next one
                        continue;
                    } else {
                        if (status != utils::QStat_EAGAIN) {
                            // overflow or error, sync
                            fprintf(stderr, "sendSync got read error from reader queue. %d, %s\n", (int)status, reader->dump_state().c_str());
                            reader->syncPos();
                        }
                    }
                    return false;
                }
            }
        };
    };
}

