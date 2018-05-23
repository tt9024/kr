/*
 * OrderIB.hpp
 *
 *  Created on: May 21, 2018
 *      Author: zfu
 */
#pragma once
#include "tpib.hpp"

namespace trader {
typedef tp::BookQ<utils::ShmCircularBuffer> IBBookQType;
typedef IBBookQType::Reader BookReaderType;

class OrderIB : public ClientBaseImp {
public :
	explicit OrderIB(int client_id = 0);

    // outgoing
	template<typename TraderType>
	int placeOrder(TraderType* trader);

	// incoming
	bool poll(); // non-blocking, invoking callback

private:
	const int _client_id;
	int _next_ord_id;
};



}
