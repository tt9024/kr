/*
 * trader.hpp
 *
 *  Created on: May 21, 2018
 *      Author: zfu
 */
#pragma once
#include <string>

namespace trader {
template<typename FloorType>
class Trader {
public:
	explicit Trader(FloorType& flr): _flr(flr) {} ;
	/*
	bool subMD();  // L1 or L2
	bool unSubMD();

	// quote updates
	void onBBO();

	// trade updates
	void onOpen();
	void onFill();
	void onCancel();
	void onReject();

	// timer events
	void onTimer();

	// control
	void start();
	void stop();
	void reload();
	void onInst();  // getting an instruction

	// MOVE to Floor
	// signal interface
	void getPosn();
	void listTrades();
	void listOpen();
	void cancelOpen();
	void cover();

private:
	// main loop
	void run();
	*/
	const std::string _name;
	FloorType& _flr;
};

}
