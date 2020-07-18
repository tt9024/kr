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

#include "IBClientBase.hpp"
#include "IBContract.hpp"
#include "circular_buffer.h"
#include "plcc/PLCC.hpp"

namespace tp {
typedef BookQ<utils::ShmCircularBuffer> IBBookQType;
typedef IBBookQType::Writer BookWriter;
typedef IBBookQType::Reader BookReader;

class TPIB : public ClientBaseImp {
private:
    int _client_id;
    const std::vector<std::string> _symL1; // the front contract l1 symbols
    const std::vector<std::string> _symL1n;// the back contract l1 symbols
    const std::vector<std::string> _symL2;
    int _next_tickerid;
    std::string _ipAddr;
    int _port;
    std::vector<IBBookQType*> _book_queue;
    std::vector<IBBookQType*> _book_queue_l1_to_l2;
    BookReader* _book_reader;  // the first L2 (or L1 if no L2) symbol
                               // for health check.  IB have problem with
                               // L2 subscription after mid night restart.
                               // No error can be detected so far.
    std::string _book_reader_sym; // the symbol for logging purpose
    volatile bool _should_run;
    int64_t _last_check_micro;  // this is used to guard against no any update
                                // and therefore cannot get the upd_micro
                                // initialized to start up micro
    void md_subscribe(const std::vector<std::string>&symL1,  // includes both l1 front and back contracts
					  const std::vector<std::string>&symL2) {
    	clearBookQueue();
    	// want live data
    	m_pClient->reqMarketDataType(1);
    	// L1, including the front and back contracts
        for (const auto& s : symL1) {
        	auto bp = new IBBookQType(BookConfig(s,"L1"),false);
        	_book_queue.push_back(bp);
            reqMDL1(s.c_str(), _next_tickerid++);
        }

        // L2
        for (const auto& s : symL2) {
        	auto bp = new IBBookQType(BookConfig(s,"L2"),false);
        	_book_queue.push_back(bp);
        	if (!_book_reader) {
        		// get the first L2 symbol, usually CL, ES or 6E
        		// should be liquid enough to be able to tell
        		_book_reader = bp->newReader();
        		_book_reader_sym = symL2[0];
        	}
            reqMDL2(s.c_str(), _next_tickerid++);
        }

        if ((!_book_reader) && (_book_queue.size() > 0)) {
        	// no L2, use L1
        	_book_reader = _book_queue[0]->newReader();
        	_book_reader_sym = symL1[0];
        }

        // for L1 to L2 queue
        // indexed by L1 symbol, with the pointer to L2 book queue
        // used for updating trades from L1 to L2 book queues
        // this is needed as L2 subscription doesn't have trade from IB
        // it is indexed as the same as the L1 queue
        for (size_t l1 = 0; l1<symL1.size(); ++l1) {
        	const auto& s = symL1[l1];
        	size_t l2 = 0;
        	for (; l2<symL2.size(); ++l2) {
        		const auto& s2 = symL2[l2];
        		if (s == s2) {
        			_book_queue_l1_to_l2.push_back(_book_queue[symL1.size()+l2]);
        			break;
        		}
        	}
        	if (l2 >= _symL2.size()) {
        		_book_queue_l1_to_l2.push_back(NULL);
        	}
        }
    }

    bool checkL2(int64_t stale_micro = 60*1000*1000LL) {

    	// check if the first L2 queue has
    	// any quote update for the last 1 minutes
    	// this is necessary since IB's mid-night restart
    	// usually throws L2 subscriptions off, no error logs...

    	if (__builtin_expect(!_book_reader, 0))
    		return true;

    	int64_t cur_micro =  utils::TimeUtil::cur_time_gmt_micro();

    	// disable checking on hours of 17, 18
    	int hour = utils::TimeUtil::utc_to_local_ymdh(cur_micro/1000000);
    	if (hour == 17 || hour == 18) {
    		return true;
    	}

    	BookDepot book;
    	// be careful here:
    	// since the L2 will be updated by the L1 trade, so
    	// the book update micro is not reliable to tell
    	// if the L2 quote has been updated for the previous stale period.
    	// So the quote update time at bid[0] and ask[0] is used instead.
    	// One more catch: upon starting, the latest update may be from
    	// previous run, so it will make it stale right way.
    	// So the very first read shouldn't be from quote. And subsequent
    	// checks cannot the time to go back.
    	if (__builtin_expect(_book_reader->getLatestUpdate(book),1)) {
    		int64_t upd_micro = getMax(book.pe[0].ts_micro, book.pe[BookLevel].ts_micro);
    		_last_check_micro = getMax(upd_micro, _last_check_micro);
    	}

    	if (__builtin_expect(_last_check_micro < cur_micro-stale_micro, 0)) {
    		logError("IBTP book %s not updated for %d seconds. Last updated at %d",
    				_book_reader_sym.c_str(),
    				(int)(stale_micro/1000000LL), (int)(_last_check_micro/1000000LL));
    		return false;
    	}
    	return true;
    }

	void reqMDL2(const char* symbol, int ticker_id, int numLevel=BookLevel) {
		if (isConnected())
		{
		    Contract con;
		    RicContract::get().makeContract(con, symbol);
            logInfo("reqMDL2: %s id: %d", symbol, ticker_id);
		    m_pClient->reqMktDepth(ticker_id, con, numLevel,TagValueListSPtr());
		} else {
			logError("reqMDDoB error not connected!");
		}
	}

	void reqMDL1(const char* symbol, int ticker_id) {
		if (isConnected())
		{
		    Contract con;
		    RicContract::get().makeContract(con,symbol);
		    std::string generic_ticks="233"; // this is RT_VOLUME
		    //std::string generic_ticks="";
            logInfo("reqMDL1: %s id: %d", symbol, ticker_id);
		    m_pClient->reqMktData(ticker_id, con, generic_ticks, false,false,TagValueListSPtr());
		} else {
			logError("reqMDBBO error not connected!");
		}
	}

	/* useless
	void reqMDTbT(const char* symbol, int ticker_id) {
		if (isConnected()) {
		    Contract con;
		    RicContract::get().makeContract(con,symbol);
		    m_pClient->reqTickByTickData(ticker_id, con, "AllLast", 0, false);
		    m_pClient->reqTickByTickData(ticker_id, con, "BidAsk", 0, true);
		} else {
			logError("req error not connected!");
		}
	}
	*/

public:
    static const int TickerStart = 2;  // this is the tickerid starts
    explicit TPIB (int client_id = 0) :
    		_client_id(client_id?client_id:plcc_getInt("TPIBClientId")),
    		_symL1(plcc_getStringArr("SubL1")),
    		_symL1n(plcc_getStringArr("SubL1n")),
			_symL2(plcc_getStringArr("SubL2")),
			_next_tickerid(TickerStart),
			_ipAddr("127.0.0.1"), _port(0),
			_book_reader(NULL), _should_run(false),
			_last_check_micro(0) {
        bool found1, found2;
        _ipAddr = plcc_getString("IBClientIP", &found1, "127.0.0.1");
        _port = plcc_getInt("IBClientPort", &found2, 0);

        if ((!found1) || (!found2)) {
            logError("TPIB failed to run - IBClientIP(%s) IBClientPort(%s)",
                    found1?"Found":"Not Found", found2?"Found":"Not Found");
            throw std::runtime_error("TPIB failed to run - required config setting not found.");
        }

        if (!_symL1.size() && !_symL2.size()) {
        	logError("TPIB started without subscription found!");
        }

        logInfo("TPIB (%s:%d) initiated with client id %d.", _ipAddr.c_str(), _port, _client_id);
    }

    void md_subscribe() {
    	std::vector<std::string> syml1 = _symL1;
    	for (const auto& s : _symL1n) {
    		syml1.push_back(s);
    	}
    	md_subscribe(syml1,_symL2);
    }

    // connect to venue
    // subscribe market data
    // and start the infinite loop
    // which will update market data bookL2, read OrderQ, write Execution/events
    void run() {
        // connect
        logInfo("TPIB started.");
        _should_run = true;
        while (_should_run) {
            if (!connect(_ipAddr.c_str(), _port, _client_id)) {
                _client_id+=1;
                _port = IBPortSwitch(_port);
                sleep(2);
                continue;
            }
            md_subscribe();
            // enter into the main loop
            const int64_t stale_micro = 30*1000*1000LL;
            _last_check_micro = utils::TimeUtil::cur_time_gmt_micro();
            uint64_t check_micro =  _last_check_micro + stale_micro/2;
            while (isConnected() && _should_run) {
            	processMessages();
            	if (utils::TimeUtil::cur_time_gmt_micro() > check_micro) {
            		if (__builtin_expect(!checkL2(stale_micro), 0)) {
            			disconnect();
            			_should_run = false;
            			break;
            		}
            		check_micro += stale_micro/2;
            	}
            }
            logInfo("TPIB disconnected.");
        }
        logInfo("TPIB stopped.");
    }

    void stop() {
        logInfo("Stopping TPIB.");
        _should_run = false;
    }

    void clearBookQueue() {
        for (auto q : _book_queue) {
        	if (q)
        		delete(q);
        }
        _book_queue.clear();
        for (auto q : _book_queue_l1_to_l2) {
        	if (q)
        		delete(q);
        }
        _book_queue_l1_to_l2.clear();
        if (_book_reader) {
        	delete _book_reader;
        	_book_reader = NULL;
        }
        _book_reader_sym = "";
    }

    ~TPIB() {
    	clearBookQueue();
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
    		logDebug("new %s %d %f\n", is_bid?"Bid":"Offer",(int)position, price);
            _book_queue[id-TickerStart]->theWriter().newPrice(price, size, position, is_bid, tm);
            break;
        case 1: // update
    		logDebug("upd %s %d %f %d\n", is_bid?"Bid":"Offer",(int)position, price, size);
        	_book_queue[id-TickerStart]->theWriter().updPrice(price, size, position, is_bid, tm);
            break;
        case 2: // del
    		logDebug("del %s %d\n", is_bid?"Bid":"Offer",(int)position);
        	_book_queue[id-TickerStart]->theWriter().delPrice(position, is_bid, tm);
            break;
        default:
            logError("IBClient received unknown operation %d", operation);
        }
    }

    void tickPrice(TickerId id, TickType field, double price, const TickAttrib& attribs) {
        logDebug("TPIB tickPrice: %llu %d %d %.7lf",
                utils::TimeUtil::cur_time_gmt_micro(), (int)(id), (int) field, price);
        switch (field) {
        case BID :
        case ASK : {
            bool is_bid = (field == BID?true:false);
            _book_queue[id-TickerStart]->theWriter().updBBOPriceOnly(price, is_bid, utils::TimeUtil::cur_time_gmt_micro());
            break;
        }
        case LAST :
        	// Trade captured via RT_VOLUME and tickstring()
            //_book_queue[id-TickerStart]->theWriter().updTrdPrice(price);
            //logInfo("TPIB tickPrice: %llu %d %d %.7lf\n",
            //        utils::TimeUtil::cur_time_gmt_micro(), (int)(id), (int) field, price);
            break;
        case CLOSE:
        	// consider putting a close in booktap
        default:
            logError("TPIB unhandled tickPrice: %llu %d %d %.7lf",
                    utils::TimeUtil::cur_time_gmt_micro(), (int)(id), (int) field, price);
        }
    }

    void tickSize(TickerId id, TickType field, int size) {
        logDebug("TPIB tickSize: %llu %d %d %d",
                utils::TimeUtil::cur_time_gmt_micro(), (int)(id), (int) field, size);
        switch (field) {
        case BID_SIZE :
        case ASK_SIZE : {
            bool is_bid = (field == BID_SIZE?true:false);
            _book_queue[id-TickerStart]->theWriter().updBBOSizeOnly(size, is_bid, utils::TimeUtil::cur_time_gmt_micro());
            break;
        }
        case LAST_SIZE :
        	/*
        	 * MOVING entire trade capture to RT_VOLUME, the SIZE and VOLUME is really off!
        	 * Sometimes the LAST SIZE could notify twice for the same RT_VOLUME update, with delay
        	 * Sometimes never
        	 * I am not sure why they keep this broken LAST SIZE and VOLUME there just to confuse people?
        	 *
        	logInfo("TRADE SIZE: %d", size);
        	if (__builtin_expect(!_book_queue[id-TickerStart]->theWriter().updTrdSize(size),0)) {
            	logError("TPIB update trade error: %s",
            			_book_queue[id-TickerStart]->theWriter().getBook()->toString().c_str());
            }
            */
            break;
        case VOLUME:
        	// This is Broken, getting back to LAST_SIZE with filtering
        	// It's IB's convention that trade is updated by price, size and a
        	// cumulative daily volume.
        	// It sucks to have to have such assumption, well, just stick to it.
            //logInfo("TRADE VOLUME: %d", size);
        	break;
        default:
            logInfo("TPIB unhandled tickSize: %llu %d %d %d",
                    utils::TimeUtil::cur_time_gmt_micro(), (int)(id), (int) field, size);
        }

    }

    // if TickType == 48, RTVolume, it's the last sale.
    void tickString(TickerId id, TickType tickType, const std::string& value) {
        //ClientBaseImp::tickString(id, tickType, value);
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
            logDebug("RT_VOLUME: %.7lf %d",price, size);
            auto& writer (_book_queue[id-TickerStart]->theWriter());
            if(__builtin_expect(!writer.updTrade(price, size),0)) {
            	logError("TPIB update trade error [RT_VOLUME price(%.7lf) size(%d)] Book:  %s",
                        price, size, writer.getBook()->toString().c_str());
            } else {
            	const BookDepot& bookL1 (writer.getBook()->_book);
				IBBookQType* q = _book_queue_l1_to_l2[id-TickerStart];
				if(q!=NULL){
					// sending the trade event captured from L1 IB subscription
					// to the L2 queue.  As the L2 IB subscription doesn't have
					// Trade (!!!)
					//
					// Simple addTrade(price,size) to the L2 book has problem.
					// L2 trade direction can be wrong when a level is removed.
					// this is because L2 and L1 updates are slightly different,
					// and the direction cannot be safely detected from L2 book.
					// Here since the direction is correct for L1, so
					// the trade direction is copied (instead of detected) for L2
					// queue.
					// Same the L2 Delta recording feeds
					q->theWriter().updTradeFromL1(price, size, bookL1);
				}
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

    void error(int id, int errorCode, const std::string& errorString)
    {
        logError( "Error id=%d, errorCode=%d, msg=%s", id, errorCode, errorString.c_str());
        switch (errorCode) {
        case 1100:
            if( id == -1) { // if "Connectivity between IB and TWS has been lost"
                disconnect();
                stop();
            }
            break;
        case 317:  // reset depth of book
        {
        	_book_queue[id-TickerStart]->theWriter().resetBook();
            logInfo("TPIB reset book %s", _book_queue[id-TickerStart]->_cfg.toString().c_str());
            break;
        }
        case 1102:
            disconnect();
            stop();
            break;

        case 2110:  //Connectivity between Trader
        	        // Workstation and server is broken.
        	        // It will be restored automatically
        case 2104:
        	        // Market data hfarm/jfarm/eufarm/usfuture/
        	        // farm connection is OK
        	        // usually after 2110
        case 2106:  // HMDS data is OK
        	// nothing can be doen
        	logInfo("Market Data disconnection/reconnection.  errorCode=%d",errorCode);
        	//disconnect();
        	break;
        case 200 :
        	// security definition not found???
        	logError("Security definition not found??? exiting... id:%d, errorCode=%d",id,errorCode);
        	//disconnect();
        	//stop();
        	break;
        default :
        	logError("Error ignored.  errorCode=%d",errorCode);
        }
    }

    void connectionClosed() {
    	disconnect();
    	logInfo( "Connection Closed");
    }

};

}
