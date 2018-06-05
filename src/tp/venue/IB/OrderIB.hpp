/*
 * OrderIB.hpp
 *
 *  Created on: May 21, 2018
 *      Author: zfu
 */
#pragma once
#include "tpib.hpp"
#include "Order.h"
#include "OrderState.h"
#include "Execution.h"
#include "Utils.h"
#include <unordered_map>

/*
 * A mininum object to hold connection to IB on order side.
 * A typical usage would be a "floor", i.e. a group of traders
 * sharing a single thread of event loop on market data updates
 * order updates and control. The order sending and updates would
 * be from this object.
 * Outgoing:
 *    - New, Can/Rep:  placeOrder
 *    - Cancel:        cancelorder
 * Incoming:
 *    - poll() :       Trader will be notified on order updates
 *
 * Note: Due to limit of next_order_id, currently only one such
 * object is allowed.  Multiple object would require a queue for
 * storing next_order_id.
 *
 * The current consideration is that this way, the order sending
 * is in-thread and fast. But if each trader require heavy
 * computation per-MDUpdate, i.e. the tight loop, then this could
 * slow down everyone in the thread.  Consider move them out
 * as dedicated thread.
 *
 */
namespace trader {
typedef tp::BookQ<utils::ShmCircularBuffer> IBBookQType;
typedef IBBookQType::Reader BookReaderType;

struct OrderInfo {
	int oid;
	int prev_oid;
	std::string sym;
	std::string action;
	int qty;
	std::string type;
	std::string tif;
	double px;
	void* trader;
	std::string status;
	OrderInfo() : oid(0), prev_oid(0), qty(0), px(0), trader(0){
	}

	OrderInfo(int this_oid, const char* symbol, const Order& ord, void* callback_trader):
		oid(this_oid),prev_oid(ord.orderId),
		sym(symbol), action(ord.action),
		qty(ord.totalQuantity), type(ord.orderType),
		tif(ord.tif), px(ord.lmtPrice),trader(callback_trader) {
	}

	void fill(Order& ord) const {
		ord.action = action;
		ord.totalQuantity = qty;
		ord.orderType = type;
		ord.tif = tif;
		ord.lmtPrice = px;
	}

	void upd(int this_oid, int newQty, double newPx) {
		prev_oid=oid;
		oid=this_oid;
		qty=newQty;
		px=newPx;
	}
};

template<typename Trader>
class OrderIB : public ClientBaseImp {
public :
	explicit OrderIB(int client_id,
			         const char* hostip,
					 int port) :
	_client_id(client_id),
	_host_ip(hostip),
	_port(port),
	_next_ord_id(1),
	_got_next_id(false),
	_should_cancel(false)  {
	};

	~OrderIB() {
		logInfo("OrderIB destructor disconnecting");
		// try cancel all open orders
		cancelAllOpen();
		disconnect();
	}

    // new and can/rep
	int placeOrder(Trader* trader,
			       const char* symbol,
				   tp::Quantity qty,
				   tp::Price price,
				   bool isBuy,
				   bool isIOC,
				   bool isLMT,
				   int org_ordid=0 // for replacing existing
				  ) {
	    Contract con;
	    RicContract::get().makeContract(con,symbol);
	    Order order;
		order.action = isBuy? "BUY":"SELL";
		order.totalQuantity = qty;
		order.orderType = isLMT?"LMT":"MKT";
		order.tif = isIOC? "IOC":"DAY";
		//order->orderType = "LMT,IOC";
		order.lmtPrice = price;
		order.transmit = true;  // just to be sure

		const int ordid = _next_ord_id++;
		// replacing?
		if (org_ordid>0) {
			order.orderId = org_ordid;
		} else {
			order.orderId = ordid;
		}

		m_pClient->placeOrder(ordid, con, order);

		logInfo("Placing Order (orgid=%d): %s %f %s at %f (new id=%d)",
				(int)order.orderId,
				order.action.c_str(),
				order.totalQuantity,
				symbol,
				order.lmtPrice,
				ordid);
		_oid_map[ordid] = new OrderInfo(ordid, symbol, order, (void*) trader);
		return ordid;
	}

    // new and can/rep
	// this doesn't quite work yet
	// TODO - fix replace
	int Replace(int org_ordid,
			    tp::Quantity size,
				tp::Price price) {
	    Contract con;  // using the default contract
	    Order order;
	    OrderInfo* oif = getByOid(org_ordid);
	    if (__builtin_expect(!oif,0)) {
	    	logError("Replace failure, org_ordid not found: %d",
	    			org_ordid);
	    	return 0;
	    }

	    oif->fill(order);
	    RicContract::get().makeContract(con,oif->sym.c_str());
	    order.orderId = org_ordid;
		order.totalQuantity = size;
		order.lmtPrice = price;
		const int ordid = _next_ord_id++;

		m_pClient->placeOrder(ordid, con, order);

		logInfo("Replacing Order(%d): %s %d %.7f oid(%d)",
				org_ordid,
				con.symbol.c_str(),
				(int)order.totalQuantity,
				order.lmtPrice,
				ordid);
		oif->upd(ordid, order.totalQuantity, order.lmtPrice);
		_oid_map[ordid] = oif;
		return ordid;
	}

	void cancelOrder(int ordid) {
		m_pClient->cancelOrder(ordid);
		logInfo("Cancel sent (%d)", ordid);
	}

	// non-blocking checking
	// returns number of messages processed
    // -1 if not connected.
	//
	int poll() {
		if (__builtin_expect(isConnected(),1)) {
			return processMessages() ;
		}
		logError("IBOrder(%d) disconnected!", _client_id);
		return -1;
	}

	bool tryConnect(int max_try = 300) {
		while (max_try > 0) {
			logInfo("IBOrder(%d) connecting %s:%d",
					_client_id, _host_ip, _port);
			if (connect(_host_ip, _port, _client_id)) {
				break;
			}
			_port = IBPortSwitch(_port);
			sleep(1);
			logInfo("IBOrder(%d) switch port trying %s:%d",
					_client_id, _host_ip, _port);
			if (connect(_host_ip, _port, _client_id)) {
				break;
			}
			logError("IBOrder(%d) failed to connect!",
					_client_id);
			sleep(1);
			_port = IBPortSwitch(_port);
			max_try -= 2;
		}
		if (max_try<=0) {
			return false;
		}
		logInfo("IBOrder(%d) connected", _client_id);
		cancelAllOpen();
		_got_next_id = false;
		m_pClient->reqIds(-1);  // get the next id;
		time_t t0 = time(NULL);
		while ( (time(NULL) <= t0+1) && (!_got_next_id)) {
			processMessages();
			usleep(1000);
		}
		if (!_got_next_id) {
			logError("Cannot get valid next id in 2 second, try again later!");
			return false;
		}
		return true;
	}

	void nextValidId(OrderId orderId) {
		_next_ord_id = orderId;
		logInfo("got next orderid: %d", _next_ord_id);
		_got_next_id = true;
	};

	// callbacks from IB
	//! [orderstatus]
	void orderStatus(OrderId orderId, const std::string& status, double filled,
			double remaining, double avgFillPrice, int permId, int parentId,
			double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice){
		logInfo("OrderStatus. Id: %ld, Status: %s, Filled: %g, Remaining: %g, "
				"AvgFillPrice: %g, PermId: %d, LastFillPrice: %g, ClientId: %d, "
				"WhyHeld: %s, MktCapPrice: %g",
				orderId, status.c_str(),
				filled, remaining,
				avgFillPrice, permId,
				lastFillPrice, clientId,
				whyHeld.c_str(), mktCapPrice);
	}
	//! [orderstatus]

	//! [openorder]
	void openOrder( OrderId orderId, const Contract& contract, const Order& order, const OrderState& ostate) {
		logInfo( "OpenOrder(%ld) %s:%s - %s (%s %s) %d@%.7f total cash(%f) status(%s) warning(%s)",
				orderId,
				contract.symbol.c_str(), contract.exchange.c_str(),
				order.action.c_str(),
				order.orderType.c_str(),
				order.tif.c_str(),
				(int)order.totalQuantity,
				order.lmtPrice,
				(double)order.cashQty == UNSET_DOUBLE ? 0 : order.cashQty,
				ostate.status.c_str(),
				ostate.warningText.c_str());
		if (order.whatIf) {
			logInfo( "What-If. ID: %ld, InitMarginBefore: %s, MaintMarginBefore: %s, EquityWithLoanBefore: %s, InitMarginChange: %s, MaintMarginChange: %s, EquityWithLoanChange: %s, InitMarginAfter: %s, MaintMarginAfter: %s, EquityWithLoanAfter: %s",
				orderId, Utils::formatDoubleString(ostate.initMarginBefore).c_str(), Utils::formatDoubleString(ostate.maintMarginBefore).c_str(), Utils::formatDoubleString(ostate.equityWithLoanBefore).c_str(),
				Utils::formatDoubleString(ostate.initMarginChange).c_str(), Utils::formatDoubleString(ostate.maintMarginChange).c_str(), Utils::formatDoubleString(ostate.equityWithLoanChange).c_str(),
				Utils::formatDoubleString(ostate.initMarginAfter).c_str(), Utils::formatDoubleString(ostate.maintMarginAfter).c_str(), Utils::formatDoubleString(ostate.equityWithLoanAfter).c_str());
		}

		if (__builtin_expect(_should_cancel, 0)) {
			cancelOrder(orderId);
		}

		/*
		auto iter = _trader_map.find(orderId);
		if (__builtin_expect(iter == _trader_map.end(),0)) {
			logError("unknown order id - %d", (int) orderId);
			return;
		}
		Trader* trd = iter->second;
		trd->onOpen();
		*/
	}
	//! [openorder]

	//! [openorderend]
	void openOrderEnd() {
		logInfo( "OpenOrderEnd");
		_should_cancel = false;
	}

	void cancelAllOpen() {
		logInfo("canceling all open orders!");
		_should_cancel = true;
		m_pClient->reqAllOpenOrders();
		while (_should_cancel) {
			processMessages();
			usleep(1000);
		}
	}

	//! [openorderend]

	//! [execdetails]
	void execDetails( int reqId, const Contract& contract, const Execution& execution) {
		logInfo( "ExecDetails. ReqId(%d) symbol(%s) execid(%s), "
				"Ord(%ld), shares(%f), lastLiq(%d), px(%.7f), time(%s)",
				reqId, contract.symbol.c_str(),
				execution.execId.c_str(),
				execution.orderId,
				execution.shares,
				execution.lastLiquidity,
				execution.price,
				execution.time.c_str());
	}
	//! [execdetails]

	//! [execdetailsend]
	void execDetailsEnd( int reqId) {
		logInfo( "ExecDetailsEnd. %d", reqId);
	}

    void error(int id, int errorCode, const std::string& errorString)
    {
        logError( "Error id=%d, errorCode=%d, msg=%s", id, errorCode, errorString.c_str());
        switch (errorCode) {
        case 1100:
            if( id == -1) // if "Connectivity between IB and TWS has been lost"
                disconnect();
            break;
        case 1102:
            disconnect();
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
        	logError("Security definition not found??? exiting...  errorCode=%d",errorCode);
        	//disconnect();
        	break;
        default :
        	logError("Error ignored.  errorCode=%d",errorCode);
        }
    }

    const OrderInfo* getOrdInfo(int oid) const {
    	auto iter = _oid_map.find(oid);
    	if (__builtin_expect(iter != _oid_map.end(),1)) {
    		return iter->second;
    	}
    	logError("OID not found: %d", oid);
    	return NULL;
    }

private:
	const int _client_id;
	const char* _host_ip;
	int _port;
	int _next_ord_id;
	std::unordered_map<int, OrderInfo*> _oid_map;
	bool _got_next_id;
	bool _should_cancel;

    OrderInfo* getByOid(int oid) {
    	auto iter = _oid_map.find(oid);
    	if (__builtin_expect( iter != _oid_map.end(), 1)) {
    		return iter->second;
    	}
    	logError("OID not found: %d", oid);
    	return NULL;
    }



};

}
