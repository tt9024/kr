/*
 * tpib.hpp
 *
 *  Created on: May 13, 2018
 *      Author: zfu
 */




#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdexcept>
#include <map>
#include <vector>
#include <unordered_map>

#include "bookL2.hpp"
#include "order.hpp"

#include "IBClientBase.hpp"
#include "IBContract.hpp"
#include "circular_buffer.h"
#include "plcc/PLCC.hpp"

namespace tp {
typedef BookQ<utils::ShmCircularBuffer> IBBookQType;
typedef IBBookQType::Writer BookWriter;

class TPIB : public ClientBaseImp {
private:
    const int _client_id;
    int _last_tickerid;
    std::string _ipAddr;
    int _port;
    BookWriter* _book_writer;
    volatile bool _should_run;

public:
    static const int TickerStart = 2;
    explicit TPIB (int client_id) : _client_id(client_id), _last_tickerid(0),
        _ipAddr("127.0.0.1"), _port(0), _book_writer(0), _should_run(false) {
        bool found0, found1, found2, found3;
        _client_id = plcc_getInt("IBClientId", &found0, 0);
        _ipAddr = plcc_getString("IBClientIP", &found1, "127.0.0.1");
        _port = plcc_getInt("IBClientPort", &found2, 0);
        const std::vector<std::string> symL1=plcc_getStringArr("SubL1", &found3);
        const std::vector<std::string> symL1=plcc_getStringArr("SubL2", &found3);
        const std::vector<std::string> symL1=plcc_getStringArr("SubTbT", &found3);

        if ((!found0) || (!found1) || (!found2) ||(!found3)) {
            logError("IBClient failed to run - config error IBClientID(%s) IBClientIP(%s) IBClientPort(%s)",
                    found0? "Found":"Not Found", found1?"Found":"Not Found", found2?"Found":"Not Found");
            throw std::runtime_error("IBClient failed to run - required config setting not found.");
        }
        logInfo("TPIB (%s:%d) initiated with client id %d.", _ipAddr.c_str(), _port, _client_id);

        std::string subs;
        for (const auto s : sym) {
        	subs += s;
        	subs += " ";
        }
        logInfo("TPIB got subs: %s",subs.c_str());
        // subscribe

        _last_tickerid = TickerStart;
        _book_writer->resetBook();

        // get symbols to subscribe
        const std::vector<BookConfig>& cfg(_book_writer->getBookConfig());
        for (size_t i=0; i<cfg.size(); ++i) {
            utils::eSecurity secid = utils::SecMappings::instance().getSecId(cfg[i].symbol.c_str());

            // subscribe to DoB
            reqMDDoB(cfg[i].symbol.c_str(), _last_tickerid, BookLevel);
            logInfo("IBClient subscribing DoB for %s, ticker_id = %d", cfg[i].symbol.c_str(), _last_tickerid);

            _tickerVec.push_back(secid);
            ++_last_tickerid;

            // also subscribe BBO in case DoB subscription is failed,
            // last trade is also obtained by this.  Don't know
            // how to merge them
            reqMDBBO(cfg[i].symbol.c_str(), _last_tickerid);
            logInfo("IBClient subscribing BBO for %s, ticker_id = %d", cfg[i].symbol.c_str(), _last_tickerid);

            _tickerVec.push_back(secid);
            ++_last_tickerid;

        }
    }

    // connect to venue
    // subscribe market data
    // and start the infinite loop
    // which will update market data bookL2, read OrderQ, write Execution/events
    void run(void*) {
        // connect
        logInfo("TPIB started.");
        _should_run = true;
        while (_should_run) {
            init();
            if (!connect(_ipAddr.c_str(), _port, _client_id)) {
                sleep(10);
                continue;
            }
            md_subscribe();
            // enter into the main loop
            while (isConnected() && _should_run) {
                sock_recv(0,0);  // spinning
                OrderInput oi;
                if (_order_reader->getNextUpdate(oi)) {
                    makeOrder(oi);
                }
            }
            logInfo("IBClient disconnected.");
        }

        logInfo("IBClient stopped.");
    }

    void stop() {
        logInfo("Stopping IBClient.");
        _should_run = false;
    }

    ~IBClient() {
        delete _order_reader;
        _order_reader = NULL;
    }

    // Market Data Stuff
    // position - level in bookL2
    // operation - 0 insert, 1 update, 2 delete
    // side - 0 ask, 1 bid
    void updateMktDepth(TickerId id, int position, int operation, int side,
                                          double price, int size) {
        // log the ticks
        unsigned long long tm = utils::TimeUtil::cur_time_gmt_micro();
        logDebug("IBClient updateMktDepth: %llu %d %d %d %d %.7lf %d\n",
                tm, (int)(id), position, operation, side, price, size);

        utils::eSecurity secid = getSecId(id);
        if (__builtin_expect((secid == utils::TotalSecurity), 0)) {
            logError("IBClient: received unknown ticker id %d, ignored.", id);
            return;
        }

        // side 0:ask 1:bid
        bool is_bid = (side == 1?true:false);
        switch (operation) {
        case 0: // new
            _book_writer->newPrice(secid, price, size, position, is_bid, tm);
            break;
        case 1: // update
            _book_writer->updPrice(secid, price, size, position, is_bid, tm);
            break;
        case 2: // del
            _book_writer->delPrice(secid, position, is_bid, tm);
            break;
        default:
            logError("IBClient received unknown operation %d", operation);
        }
    }

    void tickPrice(TickerId id, TickType field, double price, int canAutoExecute) {
        if ((field < 1) || (field > 2)) return;
        logDebug("IBClient tickPrice: %llu %d %d %d %d %.7lf %d\n",
                utils::TimeUtil::cur_time_gmt_micro(), (int)(id), 0, 1, (field==1)? 1:0, price, canAutoExecute);

        utils::eSecurity secid = getSecId(id);
        if (__builtin_expect((secid == utils::TotalSecurity), 0)) {
            logError("IBClient: received unknown ticker id %d, ignored.", id);
            return;
        }

        bool is_bid = (field == 1?true:false);
        _book_writer->updBBOPriceOnly(secid, price, is_bid, utils::TimeUtil::cur_time_gmt_micro());
    }

    void tickSize(TickerId id, TickType field, int size) {
        if ((field != 0) && (field != 3)) return;
        logDebug("IBClient tickSize: %llu %d %d %d %d %d\n",
                utils::TimeUtil::cur_time_gmt_micro(), (int)(id), 0, 1, (field==0)? 1:0, size);

        utils::eSecurity secid = getSecId(id);
        if (__builtin_expect((secid == utils::TotalSecurity), 0)) {
            logError("IBClient: received unknown ticker id %d, ignored.", id);
            return;
        }

        bool is_bid = (field == 0?true:false);
        _book_writer->updBBOSizeOnly(secid, size, is_bid, utils::TimeUtil::cur_time_gmt_micro());

    }

    // if TickType == 48, RTVolume, it's the last sale.
    void tickString(TickerId id, TickType tickType, const IBString& value) {
        if (tickType == RT_VOLUME) {
            //bool multi_fill = false;
            double price = 0;
            int size = 0;
            char buf[16];
            buf[0] = 0;
            int num_read = 0;
            sscanf(value.c_str(), "%lf;%d;%*d;%*d;%*f;%s%n", &price, &size, buf, &num_read);
            //if (num_read >= 3) {
            //    multi_fill = (buf[0] == 't');
            //}
            //logDebug("IBClient tickString: %llu %d %d %d %d %.7lf %d\n",
            //        utils::TimeUtil::cur_time_gmt_micro(), (int)(id), 0, 1, multi_fill?3:4, price, size);

            utils::eSecurity secid = getSecId(id);
            if (__builtin_expect((secid == utils::TotalSecurity), 0)) {
                logError("IBClient: received unknown ticker id %d, ignored.", id);
                return;
            }
            _book_writer->addTrade(secid, price, size, -1);

            return;
        }
        if (tickType == LAST_TIMESTAMP)
            return;
        PosixTestClient::tickString(id, tickType, value);
    }

    typedef long IBOrderId;
    struct TraderOrderInfo {
        OrderID oid;
        uint16_t tid; // trader id;
        uint16_t secid;
    };

    // only limit orders can be made for this venue
    IBOrderId makeOrder(OrderId oid, const char* symbol, int qty, double price, bool isBuy) {
        Contract contract;
        Order order;
        makeContract(contract, symbol);

        long this_oid = m_oid++;
        order.action = isBuy? "BUY":"SELL";
        order.totalQuantity = qty;
        order.orderType = "LMT";
        order.lmtPrice = price;
        order.orderId = this_oid;

        logInfo( "IBClient Placing Order %lu(%lu): %s %ld %s at %f\n", (unsigned long) oid,
                (unsigned long) this_oid, order.action.c_str(), order.totalQuantity, contract.symbol.c_str(), order.lmtPrice);
        m_pClient->placeOrder( this_oid, contract, order);
        return this_oid;
    }

    IBOrderId cancelOrder(OrderId ref_oid) {
        IBOrderId this_oid = m_ibOidMap[ref_oid];
        logInfo( "IBClient Cancel Order:  Canceling %lu(%lu)",
                (unsigned long) ref_oid, (unsigned long) this_oid);

        m_pClient->cancelOrder(this_oid);
        return this_oid;
    }

    void openOrder( IBOrderId orderId, const Contract& contract, const Order& order, const OrderState& ostate) {
        if (strcmp(ostate.status.c_str(), "Submitted") == 0) {
            orderNew(orderId);
        }
        FunctionPrint("IBClient openOrder %lu, %s, %s, %s",  (unsigned long) orderId,
                contract.ToString().c_str(), order.ToString().c_str(), ostate.ToString().c_str());
    }

    void execDetails( int reqId, const Contract& contract, const Execution& execution) {
        FunctionPrint("IBClient execDetails: %d, %s, %s", reqId, contract.ToString().c_str(), execution.ToString().c_str());
        // FillEvent
        orderFilled(execution.orderId, execution.shares, execution.price);
    }

    void error(const int id, const int errorCode, const IBString errorString)
    {
        FunctionPrint( "Error id=%d, errorCode=%d, msg=%s", id, errorCode, errorString.c_str());
        switch (errorCode) {
        case 1100:
            if( id == -1) // if "Connectivity between IB and TWS has been lost"
                disconnect();
            break;
        case 135:  // can't find OID
        case 201:  // reject
        case 110:  // wrong price
            orderRejected(id);
            break;
        case 202:  // canceled
            orderCanceled(id);
            break;
        case 317:  // reset depth of book
        {
            utils::eSecurity secid = getSecId(id);
            _book_writer->resetBook(secid);
            logInfo("IBClient reset book %s", getSymbol(secid));
            break;
        }
        case 1102:
            disconnect();
            break;
        }
    }



private:

    void orderFilled(IBOrderId orderId, int shares, double price) {
        TraderOrderInfo& info(m_oidMap[orderId]);
        _event_writer[info.tid]->eventFill(info.oid, shares, pxToInt(info.secid, price), OS_Filled);
        logInfo("IBClient Sending Fill to trader(%d) OID(%lu)", (int)info.tid, (unsigned long) info.oid);
    }

    void orderCanceled(IBOrderId orderId) {
        TraderOrderInfo& info(m_oidMap[orderId]);
        _event_writer[info.tid]->eventCanceled(info.oid);
    }

    void orderRejected(IBOrderId orderId) {
        TraderOrderInfo& info(m_oidMap[orderId]);
        _event_writer[info.tid]->eventRejected(info.oid);
    }

    void orderNew(IBOrderId orderId) {
        TraderOrderInfo& info(m_oidMap[orderId]);
        _event_writer[info.tid]->eventNew(info.oid);
    }

    // set up queues
    void init() {
        // md initializations are moved to md_subscribe
        // order specific initializations here
        m_state = ST_DISCONNECTED;
    }



    utils::eSecurity getSecId(int ticker_id) const {
        if (__builtin_expect((ticker_id <= _last_tickerid) && (ticker_id >= TickerStart), 1)) {
            return _tickerVec[ticker_id - TickerStart];
        }
        return utils::TotalSecurity;
    }

    void makeOrder(const OrderInput &oi) {
        IBOrderId iboid = 0;
        TraderOrderInfo info;
        TraderOrderInfo* infop = &info;

        switch ((OrderOps)oi._op) {
        case  OP_New :
            if ((OrderType) oi._ot != OT_Limit) {
                logError("IBClient only support limit order. Ignored Order: %s", oi.toString().c_str());
                return;
            }
            iboid = makeOrder(oi._oid,
                    getSymbol(oi._secid),
                    oi._sz,
                    pxToDouble(oi._secid, oi._px),
                    oi._side == BS_Buy
                    );

            info.oid = oi._oid;
            info.tid = oi._traderid;
            info.secid = oi._secid;

            break;
        case OP_Cancel :
            iboid = cancelOrder(oi._ref_oid);

            infop = &(m_oidMap[iboid]);
            infop->oid = oi._oid;
            break;
        default:
            logError("IBClient unknown order input: %s", oi.toString().c_str());
            return;
        }

        m_oidMap[iboid] = *infop;
        m_ibOidMap[oi._oid] = iboid;
    }

};

}
