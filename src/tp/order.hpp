#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <string>
#include <vector>
#include <unordered_map>

#include "plcc/PLCC.hpp"
#include "asset/security.hpp"

#include "time_util.h"
#include "queue.h"  // needed for SwQueue for BookQ

#define MAX_SYM_LEN 12

namespace tp {
    enum OrderType {
        OT_Limit = 1,
        OT_Market,
    };

    enum TimeInForce {
        TIF_GTC = 1,
        TIF_IOC,
    };

    enum OrderOps {
        OP_New = 1,
        OP_Replace,
        OP_Cancel,
    };

    enum BuySell {
        BS_Buy = 0,
        BS_Sell = 1
    };

    enum OrderStatus {
        OS_New = 1,
        OS_PartFill,
        OS_Filled,
        OS_Canceled,
        OS_Replaced,
        OS_Rejected,
        TP_Connected,
        TP_Disconnected,
        TP_Error
    };

    enum EventType {
        ET_Execution = 1,
        ET_OrderInput,
    };

    const char* OrderStateString(OrderStatus os) {
        switch (os) {
        case OS_New: return "NEW";
        case OS_PartFill: return "Partial Fill";
        case OS_Filled: return "Filled";
        case OS_Canceled: return "Canceled";
        case OS_Replaced: return "Replaced";
        case OS_Rejected: return "Rejected";
        case TP_Connected: return "Connected";
        case TP_Disconnected: return "Disconnected";
        case TP_Error: return "Error";
        default:
            return "unknown";
        }
    }

    typedef uint32_t OrderID;

    // normalized order
    // aligned to cache line and power of 2 for circular queue
    // note order is published by each trader, and tp will
    // scan through each trader's order queue for new orders
    // and publish execution events to its own execution queue.
    // trader should poll corresponding queues to read their events
    struct OrderInput {
#pragma pack(push,1)
        char _symbol[16];
        uint64_t _ts_micro;
        uint64_t _traderid; // could be pointer
        OrderID _oid;       // id of this order (0 for unknown)
        OrderID _ref_oid;   // id for can/rep
        Price _px;
        Quantity _sz;
        uint8_t _ot;
        uint8_t _tif;
        uint8_t _op;
        uint8_t _side;  // 0:buy, 1:sell
#pragma pack(pop)
        OrderInput() {
            memset(this, 0, sizeof(OrderInput));
        }
        std::string toString() const {
            char buf[256];
            snprintf(buf, sizeof(buf),
            		"Order Input [OID(%d) OP(%d) "
            		"SIDE(%d) PX(%d) SZ(%d) OID2(%d) SYM(%s) "
            		"OT(%d) TIF(%d) TRDER(%llu)",
                    (int) _oid, (int) _op, (int) _side, (int)
					_px, (int) _sz, (int) _ref_oid, (int) _symbol,
					(int) _ot, (int) _tif, (unsigned long long)_traderid);
            return std::string(buf);
        }
    };

    // normalized execution event
    // any reader should be able to figure out what happened
    // note the execution report is published by each venue
    // so it is implied by which queue it is from.
    struct FillEvent {
#pragma pack(push,1)
        uint64_t _ts_micro;
        Price _fill_px;
        Quantity _fill_sz;
        OrderID _oid;
        uint16_t _reserved;
        uint8_t _os;
        uint8_t _tag;
#pragma pack(pop)
        FillEvent() {
            memset(this, 0, sizeof(FillEvent));
        }
        std::string toString() const {
            char buf[256];
            snprintf(buf, sizeof(buf), "oid=%d, state=%s, size=%d, px=%d, ts=%llu",
                    (int) _oid, OrderStateString((OrderStatus) _os), (int) _fill_sz, (int) _fill_px,
                    (unsigned long long) _ts_micro);
            return std::string(buf);
        }
    };

    // TODO: work on the order side
    struct OrderInfo {
        utils::eSecurity _secid;
        utils::eVenue _venueid;
        std::vector<OrderInput*> _order_info;
        std::vector<FillEvent*> _fills;

        OrderStatus _os;
        Price _target_px;
        Price _avg_px;
        Quantity _target_sz;
        Quantity _cum_sz;

        OrderInput* _last_input;
        FillEvent* _last_event;
    };

    // this is per-venue order queue, storing
    // normalized order input
    template <template<int, int> class BufferType >
    class OrderQ {
    public:
        static const int OrderLen = sizeof(OrderInput);
        static const int QLen = (1024*OrderLen);
        const std::string _q_name;
        class Writer;
        class Reader;

        OrderQ(const char* q_name, bool readonly) :
            _q_name(q_name), _q(q_name, readonly, false)
        {
            logInfo("OrderQ %s started %s.", q_name, readonly?"ReadOnly":"ReadWrite");
        };

        // This is to enforce that for SwQueue, at most one writer should
        // be created for each BookQ
        // TODO - extend this to MwQueue of fixed length
        typedef utils::SwQueue<QLen, OrderLen, BufferType> QType;

        // client is responsible for free memory
        Writer* newWriter(int trader_id) {
            return new Writer(*this, trader_id);
        }

        Reader* newReader() {
            return new Reader(*this);
        }

    private:
        QType _q;
        friend class Writer;
        friend class Reader;

    public:
        class Writer {
        public:

            void newOrder(OrderID oid,
                    utils::eSecurity secid,
                    TimeInForce tif,
                    OrderType ot,
                    BuySell side,
                    Price px,
                    Quantity sz) {

                OrderInput oi;
                oi._oid = oid;
                oi._op = OP_New;
                oi._secid = secid;
                oi._tif = tif;
                oi._ot = ot;
                oi._side = side;
                oi._px = px;
                oi._sz = sz;
                updateQ(&oi);
            }

            void cancelOrder(OrderID oid, OrderID ref_oid) {
                OrderInput oi;
                oi._oid = oid;
                oi._ref_oid = ref_oid;
                oi._op = OP_Cancel;
                updateQ(&oi);
            }

            void replaceOrder(OrderID oid, OrderID ref_oid,
                    BuySell bs,
                    Price px,
                    Quantity sz) {
                OrderInput oi;
                oi._oid = oid;
                oi._ref_oid = ref_oid;
                oi._side = bs;
                oi._px = px;
                oi._sz = sz;
                updateQ(&oi);
            }

            ~Writer() {
                // TODO - needs to delete the _wq instance
                // once migrated to MwQueue
            }
        private:
            const int _trader_id;
            OrderQ& _oq;
            typename OrderQ::QType::Writer* _wq;  // the writer's queue

            friend class OrderQ<BufferType>;
            Writer(OrderQ& oq, int trader_id) :
                _trader_id(trader_id), _oq(oq), _wq(&(_oq._q.theWriter())) {
                // TODO the writer needs to be implemented for MwQueues
            }

            void updateQ(OrderInput* oi) {
                // Order input
                oi->_ts_micro = utils::TimeUtil::cur_time_micro();
                oi->_traderid = _trader_id;
                _wq->put((char*)oi);
            }

        };

    public:
        class Reader {
        public:
            bool getNextUpdate(OrderInput& oi) {
                utils::QStatus stat = _rq->copyNextIn((char*)&oi);
                switch (stat) {
                case utils::QStat_OK :
                    _rq->advance();
                    return true;
                case utils::QStat_EAGAIN :
                    return false;
                case utils::QStat_OVERFLOW :
                    int lost_updates = _rq->catchUp();
                    logError("venue read queue %s overflow, lost %d orders. Trying"
                            "to catch up."
                            ,_oq._q_name.c_str(), lost_updates);
                    return getNextUpdate(oi);
                }
                logError("getNextUpdate read queue %s unknown qstat %d, exiting..."
                        ,_oq._q_name.c_str(), (int) stat);
                throw std::runtime_error("OrderQ Reader got unknown qstat.");
            }

        private:
            OrderQ& _oq;
            typename OrderQ::QType::Reader* _rq;  // the reader's queue
            friend class OrderQ<BufferType>;
            Reader(OrderQ& oq) : _oq(oq), _rq(_oq._q.newReader())
            {
                // assign my read pos to the current write pos,
                // waiting for the next update
                _rq->syncPos();
            }
        };
    };

    // for testing purpose
    struct NULLOrderQ {
        class Writer {};
        class Reader {};
    };

    struct NULLEventQ {
        class Writer {};
        class reader {};
    };



    // this is per-model fill queue, storing
    // normalized fill events from venue
    template <template<int, int> class BufferType >
    class FillQ {
    public:
        static const int FillLen = sizeof(FillEvent);
        static const int QLen = (1024*FillLen);
        const std::string _q_name;
        class Writer;
        class Reader;

        FillQ(const char* q_name, bool readonly) :
            _q_name(q_name), _q(q_name, readonly, false)
        {
            logInfo("FillQ %s started %s.", q_name, readonly?"ReadOnly":"ReadWrite");
        };

        // This is to enforce that for SwQueue, at most one writer should
        // be created for each BookQ
        // TODO - extend this to MwQueue of fixed length
        typedef utils::SwQueue<QLen, FillLen, BufferType> QType;

        // client is responsible for free memory
        Writer* newWriter(int venue_id) {
            return new Writer(*this, venue_id);
        }

        Reader* newReader() {
            return new Reader(*this);
        }

    private:
        QType _q;
        friend class Writer;
        friend class Reader;

    public:
        class Writer {
        public:

            void eventFill(OrderID oid,
                    Quantity sz,
                    Price px,
                    OrderStatus os) {
                FillEvent fe;
                fe._os = OS_Filled;
                fe._fill_px = px;
                fe._fill_sz = sz;
                fe._oid = oid;
                updateQ(&fe);
            }

            void eventNew(OrderID oid) {
                FillEvent fe;
                fe._os = OS_New;
                fe._oid = oid;
                updateQ(&fe);
            }

            void eventCanceled(OrderID oid) {
                FillEvent fe;
                fe._os = OS_Canceled;
                fe._oid = oid;
                updateQ(&fe);
            }

            void eventRejected(OrderID oid) {
                FillEvent fe;
                fe._os = OS_Rejected;
                fe._oid = oid;
                updateQ(&fe);
            }

            ~Writer() {
                // TODO - needs to delete the _wq instance
                // once migrated to MwQueue
            }
        private:
            const int _venue_id;
            FillQ& _oq;
            typename FillQ::QType::Writer* _wq;  // the writer's queue

            friend class FillQ<BufferType>;
            Writer(FillQ& oq, int venue_id)
            : _venue_id(venue_id), _oq(oq), _wq(&(_oq._q.theWriter())) {
                // TODO the writer needs to be implemented for MwQueues
            }

            void updateQ(FillEvent* fe) {
                fe->_ts_micro = utils::TimeUtil::cur_time_micro();
                _wq->put((char*)fe);
            }

        };

    public:
        class Reader {
        public:
            bool getNextUpdate(FillEvent& oi) {
                utils::QStatus stat = _rq->copyNextIn((char*)&oi);
                switch (stat) {
                case utils::QStat_OK :
                    _rq->advance();
                    return true;
                case utils::QStat_EAGAIN :
                    return false;
                case utils::QStat_OVERFLOW :
                    int lost_updates = _rq->catchUp();
                    logError("venue read queue %s overflow, lost %d orders. Trying"
                            "to catch up."
                            ,_oq._q_name.c_str(), lost_updates);
                    return getNextUpdate(oi);
                }
                logError("getNextUpdate read queue %s unknown qstat %d, exiting..."
                        ,_oq._q_name.c_str(), (int) stat);
                throw std::runtime_error("FillQ Reader got unknown qstat.");
            }

        private:
            FillQ& _oq;
            typename FillQ::QType::Reader* _rq;  // the reader's queue
            friend class FillQ<BufferType>;
            Reader(FillQ& oq) : _oq(oq), _rq(_oq._q.newReader())
            {
                // assign my read pos to the current write pos,
                // waiting for the next update
                _rq->syncPos();
            }
        };
    };
}
