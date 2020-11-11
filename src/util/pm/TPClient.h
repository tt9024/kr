#pragma once

#include "FloorBase.h"
#include "ExecutionReport.h"
#include "csv_utils.h"

namespace pm {

    template<typename OrderConnection>
    class FloorClientOrder: public FloorBase {
        // FloorClientOrder act as an interface between 
        // Floor Manager and the FIX connections to venues
        // for sending orders and receiving execution reports. 
        //
        // Templated type OrderConnection needs to implement :
        // 1. errstr = sendOrder(char* data, int size)
        // 2. errstr = requestReplay(char* data, int size)
        // 3. errstr = requestOpenOrder(char* data, int size)
        // In addition, it needs to update this client with incoming :
        // 4. sendMsg(msg)

    public:
        explicit FloorClientOrder(const std::string& name, OrderConnection& conn)
        : FloorBase(name, false), m_conn(conn)
        {
            setSubscription();
        }

        ~FloorClientOrderr(){};

        // update the floor manager whenever
        void sendMsg(const FloorBase::MsgType& msg) {
            m_channel->update(msg);
        }

    protected:
        OrderConnection& m_conn;
        FloorClientOrder(const FloorClientOrder& mgr) = delete;
        FloorClientOrder& operator=(const FloorClientOrder& mgr) = delete;

        void handleMessage(MsgType& msg_in) {
            switch (msg_in.type) {
                case FloorBase::SendOrderReq {
                    handleOrderReq(msg_in);
                    break;
                }
                case FloorBase::ExecutionOpenOrderReq {
                    handleOpenOrderReq(msg_in);
                    break;
                }
                case FloorBase::ExecutionReplayReq {
                    handleExecutionReplayReq(msg_in);
                    break;
                }
                default :
                    fprintf(stderr, "unkown requests %s\n", msg_in.toString());a
            }
        }

        // incoming requests
        void setSubscriptions() {
            std::set<int> type_set;
            type_set.insert((int)FloorBase::SendOrderReq);
            type_set.insert((int)FloorBase::ExecutionOpenOrderReq);
            type_set.insert((int)FloorBase::ExecutionReplayReq);
            m_channel->addSubscription(type_set);
        }

        void handleOrderReq(const MsgType& msg) {
            const std::string errstr = m_conn.makeOrder(msg.buf, msg.data_size);
            m_channel->updateAck(msg, m_msgout, FloorBase::SendOrderAck, errstr);
        }

        void handleOpenOrderReq(const MsgType& msg) {
            const std::string errstr = m_conn.requestReplay(msg.buf, msg.data_size);
            m_channel->updateAck(msg, m_msgout, FloorBase::ExecutionRelayAck, errstr);
        }

        void handleExecutionReplayReq(const MsgType& msg) {
            const std::string errstr = m_conn.requestOpenOrder(msg.buf, msg.data_size);
            m_channel->updateAck(msg, m_msgout, FloorBase::ExecutionOpenOrderAck, errstr);
        }

        // out-going updates
        bool sendExecutionReport(const ExecutionReport& er) {
            m_msgout.type = FloorBase::ExecutionReport;
            m_msgout.copyData((char*)&er, sizeof(ExecutionReport));
            m_channel->update(m_msgout);
        }

        bool sendExecutionReplayDone() {
            m_msgout.type = FloorBase::ExecutionReplayDone;
            m_msgout.copyString("Ack");
            m_channel->update(m_msgout);
        }

        friend class FloorBase;
    };
};
