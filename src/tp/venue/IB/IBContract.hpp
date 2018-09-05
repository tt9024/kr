/*
 * RicContract.hpp
 *
 *  Created on: May 6, 2018
 *      Author: zfu
 */
#pragma once
#include "Contract.h"
#include <map>
#include <memory.h>
#include <string>
#include <ctype.h>
/*
 * This is a singleton object providing mapping of
 * IB Contract with the RIC (the Retuers thing from historical)
 * FX future contracts and European future contracts
 * are very different.
 */

class RicContract {
public:
	typedef std::string IBString;
	static const RicContract& get() {
		static RicContract instance;
		return instance;
	}

	void makeContract(Contract &con, const char* symbol, const char* curDate=NULL) const {
	    if (strncmp(symbol, "NYM", 3) == 0) {
	        makeNymContract(con, symbol);
	    } else if (strncmp(symbol, "VIX", 3) == 0) {
	        makeVixContract(con, symbol, curDate);
	    } else if (strncmp(symbol, "CME", 3) == 0) {
	        makeCmeContract(con, symbol);
	    }  else if (strncmp(symbol, "CBT", 3) == 0) {
	        makeCbtContract(con, symbol);
	    }  else if (strncmp(symbol, "EUX", 3)==0) {
	    	makeEuxContract(con, symbol);
	    }  else if (strncmp(symbol, "FX/", 3) == 0) {
	    	const char* sym0 = symbol+3;
		    if ( (strncmp(sym0, "XAU", 3) == 0) ||
		    	 (strncmp(sym0, "XAG", 3) == 0)) {
		        makeMetalContract(con, sym0);
		    } else {
		    	makeFxContract(con, sym0);
		    }
	    } else if (strncmp(symbol, "ICE", 3) == 0) {
	    	makeIceContract(con, symbol);
	    } else if (strncmp(symbol, "ETF", 3) == 0) {
	    	makeETFContract(con, symbol);
	    } else if (strncmp(symbol, "NYBOT", 5) == 0) {
	    	makeNybotContract(con, symbol);
	    }

	    else {
	    	throw std::runtime_error(std::string("unknown contract: ") + std::string (symbol));
	    }
	}

private:
	std::map<std::string, std::string> ib_cmefx;
	std::map<char, std::string> ib_futmon;
	RicContract() {
		ib_cmefx["6E"]="EUR";
		ib_cmefx["6A"]="AUD";
		ib_cmefx["6C"]="CAD";
		ib_cmefx["6B"]="GBP";
		ib_cmefx["6J"]="JPY";
		ib_cmefx["6N"]="NZD";
		ib_cmefx["6R"]="RUR";
		ib_cmefx["6Z"]="ZAR";
		ib_cmefx["6M"]="MXP";

		ib_futmon['F']="01";
		ib_futmon['G']="02";
		ib_futmon['H']="03";
		ib_futmon['J']="04";
		ib_futmon['K']="05";
		ib_futmon['M']="06";
		ib_futmon['N']="07";
		ib_futmon['Q']="08";
		ib_futmon['U']="09";
		ib_futmon['V']="10";
		ib_futmon['X']="11";
		ib_futmon['Z']="12";
	}

	// ETF prices, symbol is assumed to be ETF/EEM
	void makeETFContract(Contract &con, const char* symbol) const {
		con.symbol=std::string(symbol+4);
		con.currency = "USD";
		con.exchange = "ARCA";
		con.secType = "STK";
	}

	// FX Spot, symbol is assumed to be FX/EUR/USD
	void makeFxContract(Contract &con, const char* symbol) const {
	    con.symbol = std::string(symbol, 3);
	    con.currency = std::string(symbol+4, 3);
	    con.exchange = "IDEALPRO";
	    con.secType = "CASH";
	}

	// Metal Spot, symbol is supposed to be XAU/USD
	void makeMetalContract(Contract &con, const char* symbol) const {
	    con.symbol = std::string(symbol, 3) + std::string(symbol+4, 3);
	    con.currency = std::string(symbol+4, 3);
	    con.exchange = "SMART";
	    con.secType = "CMDTY";
	}

	// Nymex Future: invoked by symbol "NYM/CLU4", "NYM/NGU4"
	// NOTE: also include GC/SI/HG
	void makeNymContract(Contract &con, const char* symbol) const {
	    con.symbol = std::string(symbol+4, 2);
	    con.currency = "USD";
	    con.exchange = "NYMEX";
	    con.secType = "FUT";
	    con.localSymbol = std::string(symbol+4, 4);
	    con.includeExpired = true;
	}

	// ICE/LCOU8
	void makeIceContract(Contract &con, const char* symbol) const {
		if (strncmp(symbol+4,"LCO", 3) == 0) {
			con.symbol = "COIL";
		} else if (strncmp(symbol+4,"LFU", 3) == 0) {
			con.symbol = "GOIL";
		} else if (strncmp(symbol+4,"LOU", 3) == 0) {
			con.symbol = "HOIL";
		} else {
			throw std::runtime_error("unknown symbol for ICE");
		}
	    con.currency = "USD";
	    con.exchange = "IPE";
	    con.secType = "FUT";
	    con.localSymbol = con.symbol + std::string(symbol+7, 2);
	    con.includeExpired = true;
	}

	// NYBOT/CCZ8  ICE's cocoa future
	void makeNybotContract(Contract &con, const char* symbol) const {
		con.symbol = std::string(symbol+6, 2);
	    con.currency = "USD";
	    con.exchange = "NYBOT";
	    con.secType = "FUT";
	    con.localSymbol = std::string(symbol+6, 4);
	    con.includeExpired = true;
	}

	// CME future invoked by symbol "CME/ESM8"
	// NOTE: also includes FX Future such as 6E
	void makeCmeContract(Contract &con, const char* symbol) const {
	    const std::string sym = std::string(symbol+4, 2);
	    const auto iter = ib_cmefx.find(sym);
	    if (iter != ib_cmefx.end()) {
	    	con.symbol = iter->second;
	    } else {
	    	con.symbol = sym;
	    }
	    con.currency = "USD";
	    con.exchange = "GLOBEX";
	    con.secType = "FUT";
	    con.localSymbol = std::string(symbol+4, 4);
	    con.includeExpired = true;
	}

	// CBOT future invoked by symbol "CBT/ZBM8"
	// NOTE: also includes ZC
	void makeCbtContract(Contract &con, const char* symbol) const {
	    con.symbol = std::string(symbol+4, 2);
	    con.currency = "USD";
	    con.exchange = "ECBOT";
	    con.secType = "FUT";
	    //con.localSymbol = std::string(symbol+4, 2) + "  ";
	    IBString exp = "201";
	    exp+=(symbol+7);
	    exp+=ib_futmon.find(symbol[6])->second;
	    //con.tradingClass = std::string(symbol+4, 2);
	    //con.expiry=exp;
	    con.lastTradeDateOrContractMonth=exp;
	    //con.multiplier = "1000";
	    con.includeExpired = true;
	}

	// invoked by symbol "EUX/FDX"
	void makeEuxContract(Contract &con, const char* symbol) const {
		const IBString sym=std::string(symbol+4, 2);
		if (sym=="FD") {
			con.symbol = "DAX";
		    con.tradingClass="FDAX";
		} else if (sym=="ST") {
			con.symbol = "ESTX50";
		    con.tradingClass="FESX";
		} else if (sym=="FG") {
			con.symbol=std::string(symbol+5,3);
		    con.tradingClass=std::string(symbol+4,4);
		} else {
			throw std::runtime_error("eux future not found!");
		}
	    con.currency = "EUR";
	    con.exchange = "DTB";
	    con.secType = "FUT";
	    //con.localSymbol = std::string(symbol+4, 2) + "  ";
	    IBString exp = "201";
	    const size_t n = strlen(symbol);
	    exp+=(symbol+(n-1));
	    exp+=ib_futmon.find(symbol[n-2])->second;
	    //con.tradingClass = std::string(symbol+4, 2);
	    //con.expiry=exp;
	    con.lastTradeDateOrContractMonth=exp;
	    //con.multiplier = "1000";
	    con.includeExpired = true;
	}

	// invoked by symbol VIX/VXU4
	void makeVixContract(Contract &con, const char* symbol, const char* curDate) const {
	    con.symbol = std::string(symbol, 3);
	    con.currency = "USD";
	    con.exchange = "CFE";
	    con.secType = "FUT";
	    con.localSymbol = std::string(symbol+4,4);
	    con.includeExpired = true;
	}



};
