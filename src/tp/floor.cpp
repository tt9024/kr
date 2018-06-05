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
	delete _order;
	_order = NULL;
	delete _server;
	_server = NULL;
}

bool Floor::start(int max_try) {
	if (_order) {
		logError("Floor Starting Order with existing instance, killing!");
		delete _order;
		sleep (1);
	}
	logInfo("Floor starting order");
	_order=new OrderType(_client_id, _ipAddr.c_str(), _port);
	if (!_order->tryConnect(max_try)) {
		logError("Floor Order connection failed!");
		delete _order;
		return false;
	}
	return true;
}
void Floor::stop() {
	delete _order;
	_order = NULL;
	logInfo("Floor stopped");
} ;

bool Floor::bounce(bool bounceServer, int max_try) {
	if(bounceServer) {
		delete _server;
	}
	stop();
	if (bounceServer) {
		int cnt = max_try;
		while (--cnt > 0) {
			sleep(1);
			_server = new FloorServer<Floor>(*this);
			if (_server) {
				break;
			}
			logError("cannot create server in bouncing, retrying...");
		}
	}
	sleep(1);
	return start(max_try);
}

void Floor::run() {
	_should_run = true;
	start();
	while(_should_run) {
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
