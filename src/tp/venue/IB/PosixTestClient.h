/* Copyright (C) 2013 Interactive Brokers LLC. All rights reserved. This code is subject to the terms
 * and conditions of the IB API Non-Commercial License or the IB API Commercial License, as applicable. */

#ifndef posixtestclient_h__INCLUDED
#define posixtestclient_h__INCLUDED

#ifndef IB_USE_STD_STRING
#define IB_USE_STD_STRING
#endif

#include "EWrapper.h"
#include "plcc/PLCC.hpp"

#define FunctionPrint(fmt, arg...) logInfo("In %s: %s = " fmt "", __func__, #arg, ##arg);
#define FunctionPrintEmpty() logInfo("In %s", __func__);

//#define FunctionPrint(fmt, arg...)
//#define FunctionPrintEmpty()

#include <memory>

class EPosixClientSocket;

enum State {
    ST_CONNECTED,
    ST_DISCONNECTED,
    ST_ERROR
};


class PosixTestClient : public EWrapper
{
public:

	PosixTestClient();
	~PosixTestClient();

	bool sock_recv(int wait_sec = 0, int wait_msec = 0);
	void reqMDBBO(const char* symbol, int ticker_id);
	void reqMDDoB(const char* symbol, int ticker_id, int numLevel=8);

	bool connect(const char * host, unsigned int port, int clientId = 0);
	void disconnect();
	bool isConnected() const;
	virtual void onDisconnected() {};
	time_t getIdleSeconds() {
	    return time(NULL) - m_last_idle_second;
	}

protected:
	void makeFxContract(Contract &con, const char* symbol);
	void makeMetalContract(Contract &con, const char* symbol);
	void makeNymContract(Contract &con, const char* symbol);
	void makeVixContract(Contract &con, const char* symbol, const char* curDate);
    void makeContract(Contract &con, const char* symbol, const char* curDate = NULL);

public:
	// events
	void tickPrice(TickerId tickerId, TickType field, double price, int canAutoExecute);
	void tickSize(TickerId tickerId, TickType field, int size);
	void tickOptionComputation( TickerId tickerId, TickType tickType, double impliedVol, double delta,
		double optPrice, double pvDividend, double gamma, double vega, double theta, double undPrice);
	void tickGeneric(TickerId tickerId, TickType tickType, double value);
	void tickString(TickerId tickerId, TickType tickType, const IBString& value);
	void tickEFP(TickerId tickerId, TickType tickType, double basisPoints, const IBString& formattedBasisPoints,
		double totalDividends, int holdDays, const IBString& futureExpiry, double dividendImpact, double dividendsToExpiry);
	void orderStatus(OrderId orderId, const IBString &status, int filled,
		int remaining, double avgFillPrice, int permId, int parentId,
		double lastFillPrice, int clientId, const IBString& whyHeld);
	void openOrder(OrderId orderId, const Contract&, const Order&, const OrderState&);
	void openOrderEnd();
	void winError(const IBString &str, int lastError);
	void connectionClosed();
	void updateAccountValue(const IBString& key, const IBString& val,
		const IBString& currency, const IBString& accountName);
	void updatePortfolio(const Contract& contract, int position,
		double marketPrice, double marketValue, double averageCost,
		double unrealizedPNL, double realizedPNL, const IBString& accountName);
	void updateAccountTime(const IBString& timeStamp);
	void accountDownloadEnd(const IBString& accountName);
	void nextValidId(OrderId orderId) {
	    m_oid = orderId;
	}
	void contractDetails(int reqId, const ContractDetails& contractDetails);
	void bondContractDetails(int reqId, const ContractDetails& contractDetails);
	void contractDetailsEnd(int reqId);
	void execDetails(int reqId, const Contract& contract, const Execution& execution);
	void execDetailsEnd(int reqId);
	void error(const int id, const int errorCode, const IBString errorString);
	void updateMktDepth(TickerId id, int position, int operation, int side,
		double price, int size);
	void updateMktDepthL2(TickerId id, int position, IBString marketMaker, int operation,
		int side, double price, int size);
	void updateNewsBulletin(int msgId, int msgType, const IBString& newsMessage, const IBString& originExch);
	void managedAccounts(const IBString& accountsList);
	void receiveFA(faDataType pFaDataType, const IBString& cxml);
	void historicalData(TickerId reqId, const IBString& date, double open, double high,
		double low, double close, int volume, int barCount, double WAP, int hasGaps);
	void scannerParameters(const IBString &xml);
	void scannerData(int reqId, int rank, const ContractDetails &contractDetails,
		const IBString &distance, const IBString &benchmark, const IBString &projection,
		const IBString &legsStr);
	void scannerDataEnd(int reqId);
	void realtimeBar(TickerId reqId, long time, double open, double high, double low, double close,
		long volume, double wap, int count);
	void currentTime(long time) {};
	void fundamentalData(TickerId reqId, const IBString& data);
	void deltaNeutralValidation(int reqId, const UnderComp& underComp);
	void tickSnapshotEnd(int reqId);
	void marketDataType(TickerId reqId, int marketDataType);
	void commissionReport( const CommissionReport& commissionReport);
	void position( const IBString& account, const Contract& contract, int position);
	void positionEnd();
	void accountSummary( int reqId, const IBString& account, const IBString& tag, const IBString& value, const IBString& curency);
	void accountSummaryEnd( int reqId);

protected:
	std::auto_ptr<EPosixClientSocket> m_pClient;
	State m_state;
	time_t m_last_idle_second;
	OrderId m_oid;
};

#endif

