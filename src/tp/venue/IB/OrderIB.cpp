/*
 * OrderIB.cpp
 *
 *  Created on: Jun 1, 2018
 *      Author: zfu
 */

#include "OrderIB.hpp"
#include "bookL2.hpp"

#include <iostream>
#include <sstream>
#include <ostream>
#include <string>

using namespace trader;
using namespace tp;

class Trader0 {
	Trader0() {};
};

typedef trader::OrderIB<Trader0> OrderType;

// parsing a px represented by
// PX([a|b][+|-][s spdcnt|price])
tp::Price parsePx(const std::string& sym, const char* px) {
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


int run_cmd(const char* cmd, OrderType* _order) {
	logInfo("got command %s", cmd);
	std::stringstream ss(cmd);
	std::string token;
	if (!(ss >> token)) {
		logError("Got empty cmd: %s", cmd);
	}
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
		if (__builtin_expect(!oif, 0)) {
			logError("error parsing replace: oid not found %d", oid_);
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
		_order->cancelOrder(oid_);
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
		break;
	default:
		logError("unknown command: %s", cmd);
	}
	return -1;
}

std::string help() {
	char buf[1024];
	size_t n = snprintf(buf, sizeof(buf), "New Order: [B|S] SYM SZ PX([a|b][+|-][s spdcnt|price])\n");
	n+=snprintf(buf+n, sizeof(buf)-n,"Replace OrderSize:  R #OID SZ\n");
	n+=snprintf(buf+n, sizeof(buf)-n,"Replace OrderPrice: U #OID PX\n");
	n+=snprintf(buf+n, sizeof(buf)-n,"Cancel Order: C #OID\n");
	n+=snprintf(buf+n, sizeof(buf)-n,"Help: H\n");
	return std::string(buf);
}

// testings
int main(int argc, char** argv) {
    utils::PLCC::instance("ordertest");
    //int clid = plcc_getInt("OrdIBClientId");
	OrderType* _order = new OrderType(plcc_getInt("OrdIBClientId"), "127.0.0.1", plcc_getInt("IBClientPort"));
	_order->tryConnect();

	// make an order

	const char* cmd1="B NYM/CLN8 2 b-s20";
	int oid = run_cmd(cmd1, _order);
	time_t t0 = time(NULL);
	while (time(NULL) < t0 + 3) {
		_order->processMessages();
		usleep (1000);
	}


	// replace sz
	std::string cmd2= std::string("R ") + std::to_string(oid) + std::string(" 1");
	oid = run_cmd(cmd2.c_str(), _order);
	t0 = time(NULL);
	while (time(NULL) < t0 + 10) {
		_order->processMessages();
		usleep (1000);
	}

	// replace px
	std::string cmd3= std::string("U ") + std::to_string(oid) + std::string(" b-s15");
	oid = run_cmd(cmd3.c_str(), _order);
	t0 = time(NULL);
	while (time(NULL) < t0 + 10) {
		_order->processMessages();
		usleep (1000);
	}

	// replace sz
	cmd3= std::string("U ") + std::to_string(oid) + std::string(" b-s20");
	oid = run_cmd(cmd3.c_str(), _order);
	t0 = time(NULL);
	while (time(NULL) < t0 + 10) {
		_order->processMessages();
		usleep (1000);
	}


	// cancel the last one
	std::string cmd4= std::string("C ") + std::to_string(oid);
	run_cmd(cmd4.c_str(), _order);
	t0 = time(NULL);
	while (time(NULL) < t0 + 10) {
		_order->processMessages();
		usleep (1000);
	}

	delete _order;
	return 0;
}
