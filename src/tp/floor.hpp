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
#include "sys_utils.h"
#include <sstream>
#include <iostream>

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
	FloorServer(Floor& flr) : _fd(0), _flr(flr) {
		// create a floor server
		int port = plcc_getInt("FloorPort");
		_fd = utils::tcp_socket("127.0.0.1", port, true);
		logInfo("creating socket for listen on port %d", port);
		if (_fd <= 0) {
			throw std::runtime_error("cannot create a TCP Server");
		}
		// allowing only one connection at a time
		if (listen(_fd, 1)!=0) {
			perror("listen failed");
		}
	}
	// a TCP Server as a client interface
	bool poll() {
		struct sockaddr addr;
		socklen_t addrlen;
		int new_fd = accept(_fd, &addr, &addrlen);
		if (new_fd < 0) {
			if (errno == EWOULDBLOCK) {
				return true;
			}
			return false;
		}
		char buf[1441];
		size_t blen=read(new_fd, buf, sizeof(buf)-1);
		buf[blen]=0;
		logInfo("Got command: %s", buf);
		run_cmd(buf);
		close(new_fd);
		return true;
	}

	// cmd is a null terminated string
	void run_cmd(const char* cmd) {
		std::stringstream ss(std::string(cmd));
		std::string token;
		if (!(ss >> token)) {
			logError("Got empty cmd: %s", cmd);
		}
		char c1 = token.c_str[0];
		switch (c1) {
		case 'B':
		case 'S':
		{
			// make an order
			std::string sym, sz, px;
			if (!(ss >> sym) ||
				!(ss >> sz)  ||
				!(ss >> px)) {
				logError("Cannot parse New Order: %s", cmd);
				return;
			}
			tp::Quantity s = atoi(sz.c_str());
			tp::Price p = parsePx(sym, px.c_str());
			if (p==0) {
				logError("NewOrder BS: error parsing price: %s, cmd(%s)",
						px.c_str(), cmd);
				return;
			}
			_flr._order->placeOrder(NULL, sym.c_str(), s, p,
					                c1=='B',
									false, true);
			break;
		}
		case 'R':
		case 'U':
		{
			// replace an order
			// look for oid
			std::string oid, sym, tk;
			if (!(ss >> oid) ||
				!(ss >> sym) ||
				!(ss >> tk) ) {
				logError("Cannot parse replace order: %s",cmd);
				return;
			}
			if (c1 == 'R') {
				// replace sz
				_flr._order->Replace(atoi(oid.c_str()), sym.c_str(), atoi(tk.c_str()) );
			} else {
				_flr._order->Replace(atoi(oid.c_str()), sym.c_str(), 0, strtod(tk.c_str(),NULL));
			}
			break;
		}
		case 'C':
		{
			std::string oid;
			if (!(ss>>oid) ||
				!isdigit(oid.c_str[0])) {
				logError("Cannot parse cancel: %s", cmd);
				return;
			}
			_flr._order->cancelOrder(atoi(oid.c_str()));
			break;
		}
		case 'T':
			_flr.bounce(false,30);
			break;
		case 'E':
			_flr.stop();
			_flr._should_run = false;
			break;
		case 'H':
			break;
		default:
			logError("unknown command: %s", cmd);
		}
	}


	std::string help() const {
		char buf[1024];
		size_t n = snprintf(buf, sizeof(buf), "New Order: [B|S] SYM SZ PX([a|b][+|-][s spdcnt|price])\n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"Replace OrderSize:  R SYM #OID SZ\n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"Replace OrderPrice: U SYM #OID PX([a|b][+|-]price)\n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"Cancel Order: C #OID\n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"Get Position: [P] SYM\n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"Get Book: [K] SYM\n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"List Trades: [L] SYM \n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"List OpenOrder: [O]\n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"BounceOrderConnection: [T]\n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"Exit: [X]\n");
	    n+=snprintf(buf+n, sizeof(buf)-n,"Help: H\n");
	    return std::string(buf);
	}
private:
	int _fd;
	Floor _flr;

	// parsing a px represented by
	// PX([a|b][+|-][s spdcnt|price])
	tp::Price parsePx(const std::string& sym, const char* px) const {
		static const std::string l1="L1";
		// get a price from string:
		// PX([a|b][+|-][t ticks|price])
		tp::Price p=0;
		switch (px[0]) {
		case 'a':
		case 'b':
		{
			// expect [a|b][+|-][s spdcnt|price]
			tp::BookDepot book;
			if (!tp::LatestBook(sym, l1, book)) {
				logError("Couldn't get current price for %s", sym.c_str());
				return 0;
			}
			if (px[0]=='a') {
				p=book.getAsk();
			} else if (px[0] == 'b') {
				p=book.getBid();
			}
			tp::Price pxm=1;
			// need a '+' or '-' or 'END'
			switch (px[1]) {
			case '+':
				break;
			case '-':
				pxm=-1;
			    break;
			case '\0' :
				return p;
			default:
				logError("Error paring price, expect +/-/EoF: %s", px);
				return 0;
			}

			// a shift could be 's'+spdcnt or an absolute double
			tp::Price ps = 0;
			if (px[2] == 's') {
				// shift in ticks
				ps = (book.getAsk() - book.getBid()) * atoi(px+3);
			} else if (isdigit(px[2]))
			{
				// needs to be a number
			    ps = strtod(px+2,NULL);
			}  else {
				//ERROR
				logError("Error parsing price, expect s or a number: %s", px+2);
				return 0;
			}
			p += (pxm*ps);
			break;
		}
		default :
		{
			if (isdigit(px[0])) {
				p = strtod(px,NULL);
			} else {
				logError("Error parsing price, expect a nuber: %s", px);
				return 0;
			}
			break;
		};
	    };
		// normalize the p
		const double mintick=1e-10;
		p = int(p/mintick + 0.5)*mintick;
		return p;
	};
}

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
		if (_order)
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

	void run() {
		_should_run = true;
		start();
		while(_should_run) {
			_server->poll();
		}
		stop();
	}

	/*
	void reload() {};  // reload the config

	void subscribe() {};

	std::map<std::string, TraderType> _traderMap;
	*/
	typename OrderType* _order;
	typename FloorServer* _server;
	volatile bool _should_run;

private:
	int _client_id;
	std::string _ipAddr;
	int _port;

};





}
