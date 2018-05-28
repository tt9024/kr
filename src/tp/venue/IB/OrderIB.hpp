/*
 * OrderIB.hpp
 *
 *  Created on: May 21, 2018
 *      Author: zfu
 */
#pragma once
#include "tpib.hpp"
#include <unordered_map>

namespace trader {
typedef tp::BookQ<utils::ShmCircularBuffer> IBBookQType;
typedef IBBookQType::Reader BookReaderType;

template<typename Trader>
class OrderIB : public ClientBaseImp {
public :
	explicit OrderIB(int client_id = 0,
			         const char* hostip,
					 int port) :
	_client_id(client_id),
	_host_ip(hostip),
	_port(port),
	_next_ord_id(1) {
		m_pClient->reqIds(-1);  // get the next id;
	};

    // outgoing
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

		// replacing?
		if (org_ordid>0) {
			order.orderId = org_ordid;
		}
		const int ordid = _next_ord_id++;
		m_pClient->placeOrder(ordid, con, order);
		logInfo("Placing Order: %s %ld %s at %f (id=%d)",
				order.action.c_str(),
				order.totalQuantity,
				con.symbol.c_str(),
				order.lmtPrice,
				ordid);
		_trader_map[ordid] = trader;
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

	bool tryConnect() {
		logInfo("IBOrder(%d) started, connecting %s:%d",
				_client_id, _host_ip, _port);
		if (!connect(_host_ip, _port, _client_id)) {
			_port = IBPortSwitch(_port);
			logInfo("IBOrder(%d) switch port trying %s:%d",
					_client_id, _host_ip, _port);
			if (!connect(_host_ip, _port, _client_id)) {
				logError("IBOrder(%d) failed to connect!",
						_client_id);
				throw std::runtime_error("IBOrder Connection Failure!");
			}
		}
		logInfo("IBOrder(%d) connected", _client_id);
	}

	void nextValidId(int orderId) {
		_next_ord_id = orderId;
		logInfo("got next orderid: %d", _next_ord_id);
	}

private:
	const int _client_id;
	const char* _host_ip;
	int _port;
	int _next_ord_id;
	std::unordered_map<int, Trader*> _trader_map;
};

}
