/* Copyright (C) 2013 Interactive Brokers LLC. All rights reserved. This code is subject to the terms
 * and conditions of the IB API Non-Commercial License or the IB API Commercial License, as applicable. */
#ifndef IB_USE_STD_STRING
#define IB_USE_STD_STRING
#endif

#include <unistd.h>
#include <string.h>
#include <iostream>

#include "PosixTestClient.h"
#include "sdk/EPosixClientSocket.h"
#include "sdk/EPosixClientSocketPlatform.h"

#include "sdk/shared/Contract.h"
#include "sdk/shared/Order.h"
#include "sdk/shared/OrderState.h"
#include "sdk/shared/Execution.h"
#include "sdk/shared/ScannerSubscription.h"
#include "sdk/shared/CommissionReport.h"

const int PING_DEADLINE = 2; // seconds
const int SLEEP_BETWEEN_PINGS = 30; // seconds

///////////////////////////////////////////////////////////
// member funcs
PosixTestClient::PosixTestClient()
	: m_pClient(new EPosixClientSocket(this))
	, m_state(ST_DISCONNECTED)
	, m_last_idle_second(time(NULL))
    , m_oid(10000)
{
}

PosixTestClient::~PosixTestClient()
{
}

bool PosixTestClient::connect(const char *host, unsigned int port, int clientId)
{
    if (m_state == ST_CONNECTED) {
        logInfo("IB client already connected");
        return true;
    }
	// trying to connect
    logInfo( "Connecting to %s:%d clientId:%d", !( host && *host) ? "127.0.0.1" : host, port, clientId);
	bool bRes = m_pClient->eConnect( host, port, clientId);

	logInfo( "IBClient %s connected to %s:%d clientId:%d", bRes?"":"Cannot", !( host && *host) ? "127.0.0.1" : host, port, clientId);

	if (bRes) {
	    m_state = ST_CONNECTED;
	    m_last_idle_second = time(NULL);
	}
	return bRes;
}

void PosixTestClient::disconnect()
{
	m_pClient->eDisconnect();
	logInfo ( "Disconnected");
	m_state = ST_DISCONNECTED;
	onDisconnected();
}

bool PosixTestClient::isConnected() const
{
	return m_pClient->isConnected();
}

void PosixTestClient::reqMDDoB(const char* symbol, int ticker_id, int numLevel) {
	if (isConnected())
	{
	    Contract con;
	    makeContract(con, symbol);
	    m_pClient->reqMktDepth(ticker_id, con, numLevel);
	}
}

void PosixTestClient::reqMDBBO(const char* symbol, int ticker_id) {
	if (isConnected())
	{
	    Contract con;
	    makeContract(con, symbol);
	    m_pClient->reqMktData(ticker_id, con, "165,221,232,233,236,258,47,381,388", false);
	}
}

bool PosixTestClient::sock_recv(int wait_sec, int wait_msec)
{
	fd_set readSet, writeSet, errorSet;

	struct timeval tval;
	tval.tv_usec = wait_msec;
	tval.tv_sec = wait_sec;

	if( m_pClient->fd() >= 0 ) {
		FD_ZERO( &readSet);
		errorSet = writeSet = readSet;

		FD_SET( m_pClient->fd(), &readSet);

		if( !m_pClient->isOutBufferEmpty())
			FD_SET( m_pClient->fd(), &writeSet);

		FD_CLR( m_pClient->fd(), &errorSet);

		int ret = select( m_pClient->fd() + 1, &readSet, &writeSet, &errorSet, &tval);

		if( ret == 0) { // timeout
			return false;
		}

		if( ret < 0) {	// error
			disconnect();
			return false;
		}

		if( m_pClient->fd() < 0)
			return false;

		if( FD_ISSET( m_pClient->fd(), &errorSet)) {
			// error on socket
			m_pClient->onError();
			return false;
		}

		if( m_pClient->fd() < 0)
			return false;

		if( FD_ISSET( m_pClient->fd(), &writeSet)) {
			// socket is ready for writing
			m_pClient->onSend();
			return true;
		}

		if( m_pClient->fd() < 0)
			return false;

		if( FD_ISSET( m_pClient->fd(), &readSet)) {
			// socket is ready for reading
			m_pClient->onReceive();
			m_last_idle_second = time(NULL);
			return true;
		}
	}
	return false;
}
// symbol is assumed to be EUR/USD
void PosixTestClient::makeFxContract(Contract &con, const char* symbol) {
    con.symbol = std::string(symbol, 3);
    con.currency = std::string(symbol+4, 3);
    con.exchange = "IDEALPRO";
    con.secType = "CASH";
}

// symbol is supposed to be XAU/USD
void PosixTestClient::makeMetalContract(Contract &con, const char* symbol) {
    con.symbol = std::string(symbol, 3) + std::string(symbol+4, 3);
    con.currency = std::string(symbol+4, 3);
    con.exchange = "SMART";
    con.secType = "CMDTY";
}

// invoked by symbol "NYM/CLU4", "NYM/NGU4"
void PosixTestClient::makeNymContract(Contract &con, const char* symbol) {
    con.symbol = std::string(symbol+4, 2);
    con.currency = "USD";
    con.exchange = "NYMEX";
    con.secType = "FUT";
    con.localSymbol = std::string(symbol+4, 4);
    con.includeExpired = true;
    logInfo("Contract: %s", con.ToString().c_str());
}

// invoked by symbol VIX/VXU4
void PosixTestClient::makeVixContract(Contract &con, const char* symbol, const char* curDate) {
    con.symbol = std::string(symbol, 3);
    con.currency = "USD";
    con.exchange = "CFE";
    con.secType = "FUT";
    con.localSymbol = std::string(symbol+4,4);
    con.includeExpired = true;
}

void PosixTestClient::makeContract(Contract &con, const char* symbol, const char* curDate) {
    if (strncmp(symbol, "XAU", 3) == 0) {
        makeMetalContract(con, symbol);
    } else if (strncmp(symbol, "NYM", 3) == 0) {
        makeNymContract(con, symbol);
    } else if (strncmp(symbol, "VIX", 3) == 0) {
        makeVixContract(con, symbol, curDate);
    } else {
        makeFxContract(con, symbol);
    }
}


///////////////////////////////////////////////////////////////////
// events
void PosixTestClient::orderStatus( OrderId orderId, const IBString &status, int filled,
	   int remaining, double avgFillPrice, int permId, int parentId,
	   double lastFillPrice, int clientId, const IBString& whyHeld)

{
    FunctionPrint("%d, %s, %d, %d, %lf, %d, %d, %lf, %d, %s", (int)orderId, status.c_str(),
            filled, remaining, avgFillPrice, permId, parentId, lastFillPrice, clientId, whyHeld.c_str());
}

void PosixTestClient::error(const int id, const int errorCode, const IBString errorString)
{
    FunctionPrint( "Error id=%d, errorCode=%d, msg=%s", id, errorCode, errorString.c_str());
}

void PosixTestClient::tickPrice( TickerId tickerId, TickType field, double price, int canAutoExecute) {
	FunctionPrint("%ld, %d, %lf, %d", (long) tickerId, (int) field, price, canAutoExecute);
}
void PosixTestClient::tickSize( TickerId tickerId, TickType field, int size) {
	FunctionPrint("%ld, %d, %d", (long) tickerId, (int) field, size);
}
void PosixTestClient::tickOptionComputation( TickerId tickerId, TickType tickType, double impliedVol, double delta,
											 double optPrice, double pvDividend,
											 double gamma, double vega, double theta, double undPrice) {
	FunctionPrint("%ld, %d", (long) tickerId, (int) tickType);
}

void PosixTestClient::tickGeneric(TickerId tickerId, TickType tickType, double value) {
	FunctionPrint("%ld, %d, %lf", (long) tickerId, (int) tickType, value);
}

void PosixTestClient::tickString(TickerId tickerId, TickType tickType, const IBString& value) {
	FunctionPrint("%ld, %d, %s", (long) tickerId, (int) tickType, value.c_str());
}

void PosixTestClient::tickEFP(TickerId tickerId, TickType tickType, double basisPoints, const IBString& formattedBasisPoints,
							   double totalDividends, int holdDays, const IBString& futureExpiry, double dividendImpact, double dividendsToExpiry) {
	FunctionPrint("%ld, %d", (long) tickerId, (int) tickType);
}

void PosixTestClient::openOrder( OrderId orderId, const Contract& contract, const Order& order, const OrderState& ostate) {
	FunctionPrint("%ld, %s, %s, %s", (long) orderId, contract.ToString().c_str(), order.ToString().c_str(), ostate.ToString().c_str());
}

void PosixTestClient::openOrderEnd() {
	FunctionPrintEmpty();
}
void PosixTestClient::winError( const IBString &str, int lastError) {
	FunctionPrintEmpty();
}
void PosixTestClient::connectionClosed() {
	FunctionPrintEmpty();
}
void PosixTestClient::updateAccountValue(const IBString& key, const IBString& val,
										  const IBString& currency, const IBString& accountName) {
	FunctionPrint("%s, %s, %s, %s", key.c_str(), val.c_str(), currency.c_str(), accountName.c_str());
}
void PosixTestClient::updatePortfolio(const Contract& contract, int position,
		double marketPrice, double marketValue, double averageCost,
		double unrealizedPNL, double realizedPNL, const IBString& accountName){
	FunctionPrint("%s, %d, %lf, %lf, %lf, %lf, %lf, %s",
			contract.ToString().c_str(),
			position, marketPrice, marketValue, averageCost, unrealizedPNL, realizedPNL, accountName.c_str());
}
void PosixTestClient::updateAccountTime(const IBString& timeStamp) {
	FunctionPrint("%s", timeStamp.c_str());
}
void PosixTestClient::accountDownloadEnd(const IBString& accountName) {
	FunctionPrint("%s", accountName.c_str());
}
void PosixTestClient::contractDetails( int reqId, const ContractDetails& contractDetails) {
	FunctionPrint("%s", contractDetails.ToString().c_str());
}
void PosixTestClient::bondContractDetails( int reqId, const ContractDetails& contractDetails) {
	FunctionPrint("%d, %s", reqId, contractDetails.ToString().c_str());
}
void PosixTestClient::contractDetailsEnd( int reqId) {
	FunctionPrint("%d", reqId);
}
void PosixTestClient::execDetails( int reqId, const Contract& contract, const Execution& execution) {
	FunctionPrint("%d, %s, %s", reqId, contract.ToString().c_str(), execution.ToString().c_str());
}
void PosixTestClient::execDetailsEnd( int reqId) {
	FunctionPrint("%d", reqId);
}
void PosixTestClient::updateMktDepth(TickerId id, int position, int operation, int side,
									  double price, int size) {
	FunctionPrint("%ld, %d, %d, %d, %lf, %d", id, position, operation, side, price, size);
}
void PosixTestClient::updateMktDepthL2(TickerId id, int position, IBString marketMaker, int operation,
										int side, double price, int size) {
	FunctionPrint("%ld, %d, %s, %d, %d, %lf, %d", id, position, marketMaker.c_str(), operation, side, price, size);
}
void PosixTestClient::updateNewsBulletin(int msgId, int msgType, const IBString& newsMessage, const IBString& originExch) {
	FunctionPrint("%d, %d, %s, %s", msgId, msgType, newsMessage.c_str(), originExch.c_str());
}
void PosixTestClient::managedAccounts( const IBString& accountsList) {
	FunctionPrint("%s", accountsList.c_str());
}
void PosixTestClient::receiveFA(faDataType pFaDataType, const IBString& cxml) {
	FunctionPrint("%d, %s", pFaDataType, cxml.c_str());
}
void PosixTestClient::historicalData(TickerId reqId, const IBString& date, double open, double high,
									  double low, double close, int volume, int barCount, double WAP, int hasGaps) {
	FunctionPrint("%ld, %s, %lf, %lf, %lf, %lf, %d, %d, %lf, %d", reqId, date.c_str(), open, high, low,
			close, volume, barCount, WAP, hasGaps);
}

void PosixTestClient::scannerParameters(const IBString &xml) {
	FunctionPrint("%s", xml.c_str());
}
void PosixTestClient::scannerData(int reqId, int rank, const ContractDetails &contractDetails,
	   const IBString &distance, const IBString &benchmark, const IBString &projection,
	   const IBString &legsStr) {
	FunctionPrint("%d, %d, %s, %s, %s, %s, %s", reqId, rank, contractDetails.ToString().c_str(), distance.c_str(),
			benchmark.c_str(), projection.c_str(), legsStr.c_str());
}
void PosixTestClient::scannerDataEnd(int reqId) {
	FunctionPrint("%d", reqId);
}
void PosixTestClient::realtimeBar(TickerId reqId, long time, double open, double high, double low, double close,
								   long volume, double wap, int count) {
	FunctionPrint("%ld, %ld, %lf, %lf, %lf, %lf, %ld, %lf, %d", reqId, time, open, high, low, close,
			volume, wap, count);
}
void PosixTestClient::fundamentalData(TickerId reqId, const IBString& data) {
	FunctionPrint("%ld, %s", reqId, data.c_str());
}
void PosixTestClient::deltaNeutralValidation(int reqId, const UnderComp& underComp) {
	FunctionPrint("%d", reqId);
}
void PosixTestClient::tickSnapshotEnd(int reqId) {
	FunctionPrint("%d", reqId);
}
void PosixTestClient::marketDataType(TickerId reqId, int marketDataType) {
	FunctionPrint("%ld, %d", reqId, marketDataType);
}
void PosixTestClient::commissionReport( const CommissionReport& commissionReport) {
	FunctionPrint("%s", commissionReport.ToString().c_str());
}
void PosixTestClient::position( const IBString& account, const Contract& contract, int position) {
	FunctionPrint("%s, %s, %d", account.c_str(), contract.ToString().c_str(), position);
}
void PosixTestClient::positionEnd() {
	FunctionPrintEmpty();
}
void PosixTestClient::accountSummary( int reqId, const IBString& account, const IBString& tag, const IBString& value, const IBString& currency) {
	FunctionPrint("%d, %s, %s, %s, %s", reqId, account.c_str(), tag.c_str(), value.c_str(), currency.c_str());
}
void PosixTestClient::accountSummaryEnd( int reqId) {
	FunctionPrint("%d", reqId);
}

