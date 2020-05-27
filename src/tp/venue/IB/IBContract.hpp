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
	    } else if (strncmp(symbol, "IDX/", 4) == 0) {
	    	makeIdxContract(con, symbol+4);
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
        if (strcmp(con.symbol.c_str(), "VXX")==0) {
            con.exchange="ARCA";
        } else if (strcmp(con.symbol.c_str(), "GDX")==0) {
            con.exchange="ARCA";
        } else if (strcmp(con.symbol.c_str(), "UGAZ")==0) {
            con.exchange="ARCA";
        } else {
		    //con.exchange = "ARCA";
		    con.exchange = "SMART";
        }
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
    // NOTE local name doesn't work, so has to work out
    // the ten year boundary, i.e. 2010 or 2020 for '0'
	void makeCbtContract(Contract &con, const char* symbol) const {
	    con.symbol = std::string(symbol+4, 2);
	    con.currency = "USD";
	    con.exchange = "ECBOT";
	    con.secType = "FUT";
	    //con.localSymbol = std::string(symbol+4, 2) + "  ";
	    IBString exp = "20";
        const char y=symbol[7];
        exp+=( (y=='0')?'2':'1');
	    exp+=y;

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
	    IBString exp = "20";
	    const size_t n = strlen(symbol);
        const char y = symbol[n-1];
        exp+=((y=='0')?'2':'1');
	    exp+=y;
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

	// invoked by all index symbols
	// Symbol: {Currency,Exchage,localSymbol,desc,kdbSymbol}, {startHour, stopHour}, tick
	// ==========================================================================================
	// IDX/ATX     {EUR, VSE,     ATXF,  "Austrian Trading Index", ATX   }, {03:00, 11:00}, 0.01
	// IDX/AP      {AUD, ASX,     AP  ,  "Australian ASX200"     , AXJO  }, {17:50, 00:30}, 0.001
	// IDX/TSX     {CAD, TSE,     TSX ,  "Toranto TSE Composit"  , GSPTSE}, {09:30, 17:00}, 0.01
	// IDX/HSI     {HKD, HKFE,    HSI ,  "Hang Seng Stock Index" , HSI   }, {20:30, 03:00}, 0.01
	// IDX/K200    {KRW, KSE,     KS200, "Korean KSE 200"        , KS11  }, {18:30, 01:45}, 0.01
	// IDX/Y       {GBP, ICEEU,   Y,     "London FTSE 250 Index" , MCX   }, {03:00, 14:00}, 0.5
	// IDX/MXY     {USD, PSE,     MXY,   "Mexico Bolsa Idx"      , MXX   }, {09:30, 16:00}, 0.01
	// IDX/N225    {JPY, OSE.JPN, N225,  "Nekei 225"             , N225  }, {19:00, 01:00}, 0.1
	// IDX/OMXS30  {SEK, OMS,     OMXS30,"Sweden OMXS30"         , OMXS30}, {03:00, 11:25}, 0.01
	// IDX/VIX     {USD, CBOE,    VIX,   "Sector:Indices"        , VIX   }, {03:00, 16:15}, 0.01
	void makeIdxContract(Contract &con, const char* symbol) const {
        if (strcmp(symbol, "ATX")==0) {
            con.symbol = "ATX";
            con.currency = "EUR";
            con.secType = "IND";
            con.exchange = "VSE";
            con.localSymbol = "ATXF";
        } else if (strcmp(symbol, "VIX")==0) {
            con.symbol = "VIX";
            con.currency="USD";
            con.secType="IND";
            con.exchange="CBOE";
        } else if (strcmp(symbol, "Y")==0) {
            con.symbol = "Y";
            con.currency="GBP";
            con.secType="IND";
            con.exchange="ICEEU";
        } else if (strcmp(symbol, "N225")==0) {
            con.symbol = "N225";
            con.currency="JPY";
            con.secType="IND";
            con.exchange="OSE.JPN";
        } else if (strcmp(symbol,"HSI")==0) {
            con.symbol = "HSI";
            con.currency="HKD";
            con.secType="IND";
            con.exchange="HKFE";
        } else if (strcmp(symbol, "K200")==0) {
            con.symbol="K200";
            con.currency="KRW";
            con.secType="IND";
            con.exchange="KSE";
            con.localSymbol="KS200";
        } else if (strcmp(symbol, "OMXS30")==0) {
            con.symbol="OMXS30";
            con.currency="SEK";
            con.exchange="OMS";
            con.secType="IND";
        } else if (strcmp(symbol, "AP")==0) {
            con.symbol="AP";
            con.currency="AUD";
            con.exchange="ASX";
            con.secType="IND";
        } else if (strcmp(symbol, "TSX") == 0) {
            con.symbol="TSX";
            con.currency="CAD";
            con.exchange="TSE";
            con.secType="IND";
        } else if (strcmp(symbol,"MXY")==0){
            con.symbol="MXY";
            con.currency="USD";
            con.secType="IND";
            con.exchange="PSE";
        }
	}

};
