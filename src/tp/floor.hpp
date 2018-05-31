/*
 * floor.hpp
 *
 *  Created on: May 30, 2018
 *      Author: zfu
 */

#pragma once
#include <bookL2.hpp>
#include <OrderIB.hpp>
#include <trader.hpp>

namespace trader {

/*
 * Floor is a single thread, holding a client connection
 * with IB.  The main loop involves running Order updates
 * and run trader's callback, checking market data updates
 * and run trader's callback, checking control input to provide
 * a client interface for start/stop/reload/trade/position.
 *
 */

class Floor;
typedef Trader<Floor> TraderType;
typedef OrderIB< typename TraderType> OrderType;

class FloorServer {
public:

	FloorServer(Floor& flr);
	// a TCP Server as a client interface
	void poll();     // nonblocking checking

private:
	int _fd;
	Floor _flr;
};

class Floor {
public:
	Floor() :  _order(0), _server(0) {
		bool found1, found2;
        _ipAddr = plcc_getString("IBClientIP", &found1, "127.0.0.1");
        _port = plcc_getInt("IBClientPort", &found2, 0);

        if ((!found1) || (!found2)) {
            logError("Floor failed to run - IBClientIP(%s) IBClientPort(%s)",
                    found1?"Found":"Not Found", found2?"Found":"Not Found");
            throw std::runtime_error("Floor failed to run - required config setting not found.");
        }
        _client_id = plcc_getInt("OrdIBClientId");
		logInfo("Floor starting FloorServer");
		_server = new FloorServer(*this); // this should never die, otherwise kill floor
	}
	~Floor() {
		logInfo("Floor Destructor stop()");
		stop();
		delete _order;
		_order = NULL;
		delete _server;
		_server = NULL;
	}

	bool start(int max_try = 2) {
		if (_order) {
			logError("Floor Starting Order with existing instance, killing!");
			delete _order;
			sleep (1);
		}
		logInfo("Floor starting order");
		_order=new typename OrderType(_client_id, _ipAddr.c_str(), _port);
		if (!_order->tryConnect(max_try)) {
			logError("Floor Order connection failed!");
			delete _order;
			return false;
		}
		return true;
	}
	void stop() {
		_order->disconnect();
		delete _order;
		_order = NULL;
		logInfo("Floor stopped");
	} ;

	bool bounce(bool bounceServer = false, int max_try = 2) {
		if(bounceServer) {
			delete _server;
			_server = new FloorServer(*this);
		}
		stop();
		return start(max_try);
	}

	/*
	void reload() {};  // reload the config

	void subscribe() {};

	std::map<std::string, TraderType> _traderMap;
	*/
	typename OrderType* _order;
	typename FloorServer* _server;

private:
	int _client_id;
	std::string _ipAddr;
	int _port;

};



}
