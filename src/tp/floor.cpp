/*
 * floor.cpp
 *
 *  Created on: Jun 4, 2018
 *      Author: zfu
 */

#include "floor.hpp"

namespace trader {

/*
 * Floor
 */

Floor::Floor() :  _order(0), _server(0) {
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
	_server = new FloorServer<Floor>(*this); // this should never die, otherwise kill floor
	if (!_server) {
		throw std::runtime_error("Floor failed to run - cannot create server!");
	}
}
Floor::~Floor() {
	logInfo("Floor Destructor stop()");
	stop();
	if (_order)
		delete _order;
	_order = NULL;
	if (_server)
		delete _server;
	_server = NULL;
}

bool Floor::start(int max_try, bool cancel_all_open) {
	/*
	if (_order) {
		logError("Floor Starting Order with existing instance, killing!");
		delete _order;
		sleep (1);
	}
	*/
	logInfo("Floor starting order");
	if (_order) {
		logInfo("deleting existing order instance");
		delete _order;
		sleep(2);
		_order = NULL;
	}
	_order=new OrderType(_client_id, _ipAddr.c_str(), _port);
	if (!_order->tryConnect(max_try, cancel_all_open)) {
		logError("Floor Order connection failed!");
		return false;
	}
	return true;
}
void Floor::stop() {
	// calling stop will delete all open orders
	delete _order;
	_order = NULL;
	logInfo("Floor stopped");
} ;

bool Floor::bounce(bool bounceServer, int max_try) {
	// bounce does not delete open orders
	if(bounceServer) {
		logInfo("Deleting server");
		delete _server;
	}
	_order->disconnect();
	if (bounceServer) {
		logInfo("creating server");
		int cnt = max_try;
		while (--cnt > 0) {
			_server = new FloorServer<Floor>(*this);
			if (_server) {
				break;
			}
			sleep(1);
			logError("cannot create server in bouncing, retrying...");
		}
	}
	sleep(1);
	return start(max_try, false);
}

void Floor::run() {
	_should_run = true;
	start(300, false);
	while(_should_run) {
		if (__builtin_expect(!_order->isConnected(),0)) {
			if (!start(30, false)) {
				logInfo("wait for 1 second and retry ");
				sleep(1);
				continue;
			}
		}
		if (__builtin_expect(!_server->poll(), 0)) {
			bounce(true);
		}
	}
	stop();
}
} // namespace trader

using namespace trader;
int main() {
	utils::PLCC::instance("floor");
	Floor flr;
	flr.run();
	return 0;
}
