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
typedef OrderIB<TraderType> OrderType;

template<typename FLOOR>
class FloorServer {
public:
	explicit FloorServer(FLOOR& flr);
	// a TCP Server as a client interface
    ~FloorServer();
	bool poll();
	int run_cmd(const char* cmd);
	std::string help() const;

private:
	int _fd;
	std::map<int, struct sockaddr> _fdmap;
	FLOOR& _flr;

	// parsing a px represented by
	// PX([a|b][+|-][s spdcnt|price])
	tp::Price parsePx(const std::string& sym, const char* px);
};  // class FloorServer

class Floor {
public:
	Floor();
	~Floor();
	bool start(int max_try = 30, bool cancel_all_open=false);
	void stop();
	bool bounce(bool bounceServer = false, int max_try = 30);
	void run();

	/*
	void reload() {};  // reload the config

	void subscribe() {};

	std::map<std::string, TraderType> _traderMap;
	*/
	OrderType* _order;
	FloorServer<Floor>* _server;
	volatile bool _should_run;

private:
	int _client_id;
	std::string _ipAddr;
	int _port;
};

template<typename FLOOR>
FloorServer<FLOOR>::FloorServer(FLOOR& flr) : _fd(0), _flr(flr) {
	// create a floor server
	int port = plcc_getInt("FloorPort");
	const char* ip = "127.0.0.1";
	_fd = utils::tcp_socket(ip, port, false, false, true);
	logInfo("creating socket for listen on port %d", port);
	if (_fd <= 0) {
		throw std::runtime_error("cannot create a TCP Server");
	}
	// allowing only one connection at a time
	if (listen(_fd, 5)!=0) {
		perror("listen failed");
	}
	logInfo("Server listening on %s:%d", ip, port);
}
template<typename FLOOR>
inline
FloorServer<FLOOR>::~FloorServer() {
	close(_fd);
	for (auto iter=_fdmap.begin(); iter!=_fdmap.end(); ++iter) {
		close(iter->first);
	}
}
// a TCP Server as a client interface
template<typename FLOOR>
inline
bool FloorServer<FLOOR>::poll() {
	while (_flr._order->processMessages()) {};
	// check subscriptions

	// check new connection
	struct sockaddr addr;
	socklen_t addrlen;
	int new_fd = accept(_fd, &addr, &addrlen);
	if (__builtin_expect(new_fd < 0, 1)) {
		if (__builtin_expect(errno != EWOULDBLOCK,0)) {
			return false;
		}
	} else {
		// got a new connection,
		// set nonblocking/nodelay
		logInfo("Got a new connection: fd(%d) %s",
				new_fd, utils::print_sockaddr(addr).c_str());
		utils::set_socket_non_blocking(new_fd);
		utils::set_socket_nodelay(new_fd);
		_fdmap[new_fd] = addr;
	}

	// check all current connections
	char buf[2048];
	for (auto iter=_fdmap.begin(); iter!=_fdmap.end();++iter) {
		int fd = iter->first;
		ssize_t blen=read(fd, buf, sizeof(buf)-1);
		if (__builtin_expect(blen > 0, 0)) {
			ssize_t n;
			// blocking read all
			while ((n=read(fd, buf+blen, sizeof(buf)-blen-1))>0) {
				blen+=n;
			}
			buf[blen]=0;
			int ret = run_cmd(buf);
			std::string rstr=std::to_string(ret);
			write(fd, rstr.c_str(), rstr.length()+1);
			logInfo("fd(%d) Got command: %s, return (%d)", fd, buf, ret);
		} else {
			if (__builtin_expect( ((blen<0) && (errno != EAGAIN)),0)) {
				// disconnected?
				logInfo("fd(%d) %s disconnected! errno(%d), erased fd", fd,
						utils::print_sockaddr(iter->second), errno);
				_fdmap.erase(iter);
				break;
			}
		}
	}
	return true;
}

template<typename FLOOR>
int FloorServer<FLOOR>::run_cmd(const char* cmd) {
	logInfo("got command %s", cmd);
	std::stringstream ss(cmd);
	std::string token;
	if (!(ss >> token)) {
		logError("Got empty cmd: %s", cmd);
	}
	OrderType* _order = _flr._order;
	char c1 = token.c_str()[0];
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
			return -1;
		}
		tp::Quantity s = atoi(sz.c_str());
		tp::Price p = parsePx(sym, px.c_str());
		if (p==0) {
			logError("NewOrder BS: error parsing price: %s, cmd(%s)",
					px.c_str(), cmd);
			return -1;
		}
		return _order->placeOrder(NULL, sym.c_str(), s, p,
								c1=='B',
								false, true);
	}
	case 'R':
	case 'U':
	{
		// replace an order
		// look for oid
		std::string oid, tk;
		if (!(ss >> oid) ||
			!(ss >> tk) ) {
			logError("Cannot parse replace order: %s",cmd);
			return -1;
		}
		int oid_ = atoi(oid.c_str());
		const OrderInfo* oif = _order->getOrdInfo(oid_);
		if (__builtin_expect(!oif || !oif->isOpen, 0)) {
			logError("error parsing replace: oid not found "
					"or not open %d", oid_);
			return -1;
		}
		// the cancel replace doesn't quite work, do
		// cancel old order for now
		int new_oid=-1;
		if (c1 == 'R') {
			// replace sz
			new_oid = _order->Replace(oid_, atoi(tk.c_str()), oif->px);
		} else {
			// replace px, get the price
			const std::string& sym(oif->sym);
			tp::Price p = parsePx(sym, tk.c_str());
			new_oid = _order->Replace(oid_, oif->qty, p);
		}
		// don't know why I have to cancel the oid_ ???
		//_order->cancelOrder(oid_);
		return new_oid;
	}
	case 'C':
	{
		std::string oid;
		if (!(ss>>oid) ||
			!isdigit(oid.c_str()[0])) {
			logError("Cannot parse cancel: %s", cmd);
			return -1;
		}
		_order->cancelOrder(atoi(oid.c_str()));
		return atoi(oid.c_str());
	}
	case 'H':
		return 0;
	case 'T':
		_flr.bounce(false,30);
		return 0;
	case 'E':
		_flr.stop();
		_flr._should_run = false;
		return 0;
	case 'X':
		_order->cancelAllOpen();
		return 0;
	case 'O':
		_order->ListOpen();
		return 0;
	case 'P':
		_order->reqPositions();
		return 0;
	default:
		logError("unknown command: %s", cmd);
		return -1;
	}
	return -1;
};

// parsing a px represented by
// PX([a|b][+|-][s spdcnt|price])
template<typename FLOOR>
tp::Price FloorServer<FLOOR>::parsePx(const std::string& sym, const char* px) {
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
	// normalize the p? not necessary for now
	//const double mintick=1e-10;
	//p = static_cast<int>(p/mintick + 0.5)*mintick;
	return p;
};

template<typename FLOOR>
inline
std::string FloorServer<FLOOR>::help() const {
	char buf[1024];
	size_t n = snprintf(buf, sizeof(buf), "New Order: [B|S] SYM SZ PX([a|b][+|-][s spdcnt|price])\n");
	n+=snprintf(buf+n, sizeof(buf)-n,"Replace OrderSize:  R #OID SZ\n");
	n+=snprintf(buf+n, sizeof(buf)-n,"Replace OrderPrice: U #OID PX\n");
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
}
