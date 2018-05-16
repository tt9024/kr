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
    const std::vector<std::string> _symL1;
    const std::vector<std::string> _symL2;
    const std::vector<std::string> _symTbT;
    int _next_tickerid;
    std::string _ipAddr;
    int _port;
    std::vector<IBBookQType*> _book_queue;
    volatile bool _should_run;
    void md_subscribe(const std::vector<std::string>&symL1,
    		          const std::vector<std::string>&symTbT,
					  const std::vector<std::string>&symL2) {
    	// want live data
    	m_pClient->reqMarketDataType(1);

    	// L1
        for (const auto& s : symL1) {
        	auto bp = new IBBookQType(BookConfig(s,"L1"),false);
        	_book_queue.push_back(bp);
            reqMDL1(s.c_str(), _next_tickerid++);
        }

    	// TbT
        for (const auto& s : symTbT) {
        	auto bp = new IBBookQType(BookConfig(s,"TbT"),false);
        	_book_queue.push_back(bp);
            reqMDTbT(s.c_str(), _next_tickerid++);
        }

        // L2
        for (const auto& s : symL2) {
        	auto bp = new IBBookQType(BookConfig(s,"L2"),false);
        	_book_queue.push_back(bp);
            reqMDL2(s.c_str(), _next_tickerid++);
        }
    }

public:
    static const int TickerStart = 2;
    explicit TPIB (int client_id) : _client_id(client_id),
    		_symL1(plcc_getStringArr("SubL1")),
			_symL2(plcc_getStringArr("SubL2")),
			_symTbT(plcc_getStringArr("SubTbT")),
			_next_tickerid(TickerStart),
			_ipAddr("127.0.0.1"), _port(0), _should_run(false) {
        bool found0, found1, found2;
        _client_id = plcc_getInt("IBClientId", &found0, 0);
        _ipAddr = plcc_getString("IBClientIP", &found1, "127.0.0.1");
        _port = plcc_getInt("IBClientPort", &found2, 0);

        if ((!found0) || (!found1) || (!found2)) {
            logError("TPIB failed to run - config error IBClientID(%s) IBClientIP(%s) IBClientPort(%s)",
                    found0? "Found":"Not Found", found1?"Found":"Not Found", found2?"Found":"Not Found");
            throw std::runtime_error("TPIB failed to run - required config setting not found.");
        }

        if (!_symL1.size() && !_symL2.size() && !_symTbT.size()) {
        	logError("TPIB started without subscription found!");
        }

        logInfo("TPIB (%s:%d) initiated with client id %d.", _ipAddr.c_str(), _port, _client_id);
    }

    void md_subscribe() {
    	md_subscribe(_symL1,_symTbT,_symL2);
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
            if (!connect(_ipAddr.c_str(), _port, _client_id)) {
                sleep(10);
                continue;
            }
            md_subscribe();
            // enter into the main loop
            while (isConnected() && _should_run) {
            	processMessages();
            }
            logInfo("IBClient disconnected.");
        }
        logInfo("IBClient stopped.");
    }

    void stop() {
        logInfo("Stopping IBClient.");
        _should_run = false;
    }

    ~TPIB() {
        for (auto&q : _book_queue) {
        	delete(q);
        }
    }

    // Market Data Stuff
    // position - level in bookL2
    // operation - 0 insert, 1 update, 2 delete
    // side - 0 ask, 1 bid
    void updateMktDepth(TickerId id, int position, int operation, int side,
                                          double price, int size) {
        // log the ticks
        unsigned long long tm = utils::TimeUtil::cur_time_gmt_micro();
        logDebug("TPIB updateMktDepth: %llu %d %d %d %d %.7lf %d\n",
                tm, (int)(id), position, operation, side, price, size);

        // side 0:ask 1:bid
        bool is_bid = (side == 1?true:false);
        switch (operation) {
        case 0: // new
            _book_queue[id-TickerStart]->theWriter().newPrice(price, size, position, is_bid, tm);
            break;
        case 1: // update
        	_book_queue[id-TickerStart]->theWriter().updPrice(price, size, position, is_bid, tm);
            break;
        case 2: // del
        	_book_queue[id-TickerStart]->theWriter().delPrice(position, is_bid, tm);
            break;
        default:
            logError("IBClient received unknown operation %d", operation);
        }
    }

    void tickPrice(TickerId id, TickType field, double price, const TickAttrib& attribs) {
        logInfo("TPIB tickPrice: %llu %d %d %.7lf\n",
                utils::TimeUtil::cur_time_gmt_micro(), (int)(id), (int) field, price);
        switch (field) {
        case BID :
        case ASK :
            bool is_bid = (field == BID?true:false);
            _book_queue[id-TickerStart]->theWriter().updBBOPriceOnly(price, is_bid, utils::TimeUtil::cur_time_gmt_micro());
            break;
        case LAST :
            _book_queue[id-TickerStart]->theWriter().updTrdPrice(price);
            break;
        default:
            logError("TPIB unhandled tickPrice: %llu %d %d %.7lf\n",
                    utils::TimeUtil::cur_time_gmt_micro(), (int)(id), (int) field, price);
        }
    }

    void tickSize(TickerId id, TickType field, int size) {
        logInfo("TPIB tickSize: %llu %d %d %d\n",
                utils::TimeUtil::cur_time_gmt_micro(), (int)(id), (int) field, size);
        switch (field) {
        case BID_SIZE :
        case ASK_SIZE :
            bool is_bid = (field == BID_SIZE?true:false);
            _book_queue[id-TickerStart]->theWriter().updBBOSizeOnly(size, is_bid, utils::TimeUtil::cur_time_gmt_micro());
            break;
        case LAST :
            if (__builtin_expect(!_book_queue[id-TickerStart]->theWriter().updTrdSize(size),0)) {
            	logError("TPIB update trade error: %s",
            			_book_queue[id-TickerStart]->theWriter().getBook()->toString().c_str());
            }
            break;
        default:
            logError("TPIB unhandled tickSize: %llu %d %d %d\n",
                    utils::TimeUtil::cur_time_gmt_micro(), (int)(id), (int) field, size);
        }

    }

    // if TickType == 48, RTVolume, it's the last sale.
    void tickString(TickerId id, TickType tickType, const std::string& value) {
        ClientBaseImp::tickString(id, tickType, value);
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
            if(__builtin_expect(!_book_queue[id-TickerStart]->theWriter().updTrade(price, size),0)) {
            	logError("TPIB update trade error: %s",
            			_book_queue[id-TickerStart]->theWriter().getBook()->toString().c_str());
            }
            return;
        }
    }

    void tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice, int bidSize, int askSize, const TickAttrib& attribs) {
        logInfo("Tick-By-Tick. ReqId: %d, TickType: BidAsk, Time: %s, BidPrice: %g, AskPrice: %g, BidSize: %d, AskSize: %d, BidPastLow: %d, AskPastHigh: %d",
            reqId, ctime(&time), bidPrice, askPrice, bidSize, askSize, attribs.bidPastLow, attribs.askPastHigh);
    }

    void tickByTickAllLast(int reqId, int tickType, time_t time, double price, int size, const TickAttrib& attribs, const std::string& exchange, const std::string& specialConditions) {
        logInfo("Tick-By-Tick. ReqId: %d, TickType: %s, Time: %s, Price: %g, Size: %d, PastLimit: %d, Unreported: %d, Exchange: %s, SpecialConditions:%s",
            reqId, (tickType == 1 ? "Last" : "AllLast"), ctime(&time), price, size, attribs.pastLimit, attribs.unreported, exchange.c_str(), specialConditions.c_str());
    }

    void error(const int id, const int errorCode, const std::string errorString)
    {
        logError( "Error id=%d, errorCode=%d, msg=%s", id, errorCode, errorString.c_str());
        switch (errorCode) {
        case 1100:
            if( id == -1) // if "Connectivity between IB and TWS has been lost"
                disconnect();
            break;
        case 317:  // reset depth of book
        {
        	_book_queue[id-TickerStart]->theWriter().resetBook();
            logInfo("TPIB reset book %s", _book_queue[id-TickerStart]->_cfg.toString().c_str());
            break;
        }
        case 1102:
            disconnect();
            break;
        }
        default :
        	logError("Error ignored.  errorCode=%d",errorCode);
    }

};

}
