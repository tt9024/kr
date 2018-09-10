/*
 * featcol.hpp
 *
 *  Created on: Sep 8, 2018
 *      Author: zfu
 *

This defines an interface from market data ingestion to indicator/feature collection.
The collector is notified on continuous book updates (each represented by a BookDepot),
such as quote/trades.

Specific indicator/feature subclass the base api to subscribe to book updates.
 */

#include "bookL2.hpp"
#pragma once

class FeatCol {
public:
	FeatCol(const char* name, const char* cfg_file);
	virtual ~FeatCol();

	virtual void onQuote(const tp::BookDepot& book, bool isBid, int level);
	virtual void onTrade(const tp::BookDepot& book, bool isBuy, int size);

};


class Collector {
public:
	Collector(const char* name, const char* cfg_file);
	void addFeatCol(FeatCol* feat);
private:
	std::vector<FeatCol*> _feat_arr;


};
