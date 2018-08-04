/*
 * IBClientBase.cpp
 *
 *  Created on: May 6, 2018
 *      Author: zfu
 */

// include needed for wrapper callbacks
#include "IBClientBase.hpp"
#include "IBContract.hpp"
#include "Order.h"
#include "OrderState.h"
#include "Execution.h"
#include "CommissionReport.h"
#include "ScannerSubscription.h"
#include "executioncondition.h"
#include "PriceCondition.h"
#include "MarginCondition.h"
#include "PercentChangeCondition.h"
#include "TimeCondition.h"
#include "VolumeCondition.h"
#include "CommonDefs.h"
#include "TagValue.h"
#include "Utils.h"

#include "plcc/PLCC.hpp"
#include <iostream>

// The connectivity implementations

ClientBaseImp::ClientBaseImp(int to_milli) :
      m_osSignal(to_milli)//millisecond blocking, 0 non-blocking
    , m_pClient(new EClientSocket(this, &m_osSignal))
    , m_pReader(0)
    , m_extraAuth(false)
    , m_errorCode(0)
{
}

//! [socket_init]
ClientBaseImp::~ClientBaseImp()
{
    if (m_pReader)
        delete m_pReader;
    delete m_pClient;
}

bool ClientBaseImp::connect(const char *host, unsigned int port, int clientId)
{
	logInfo("Connecting to %s:%d clientId:%d", !( host && *host) ? "127.0.0.1" : host, port, clientId);
	bool bRes = m_pClient->eConnect( host, port, clientId, m_extraAuth);
	if (bRes) {
		logInfo( "Connected to %s:%d clientId:%d", m_pClient->host().c_str(), m_pClient->port(), clientId);
        m_pReader = new EReader(m_pClient, &m_osSignal);
		m_pReader->start();
	}
	else
		logInfo( "Cannot connect to %s:%d clientId:%d", m_pClient->host().c_str(), m_pClient->port(), clientId);
	return bRes;
}

void ClientBaseImp::disconnect() const
{
	m_pClient->eDisconnect();
	logInfo("Disconnected");
}

bool ClientBaseImp::isConnected() const
{
	return m_pClient->isConnected();
}

int ClientBaseImp::processMessages()
{
	int processed_count = 0;
	int errcode = m_osSignal.waitForSignal();
	if (errcode== 0) {
		// this can fall through if timeout
		errno = 0;
		processed_count = m_pReader->processMsgs();
	} else {
		if (errcode != ETIMEDOUT) {
			logError("error encounterd at waitForSignal, %d", errcode);
			return -1;
		}
	}
	return processed_count;
}

////////////////////////////////////////////////////////////////
// whole bunch of callbacks from EWrapper implementation,
// default behavior is just log
//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
// methods
//! [connectack]
void ClientBaseImp::connectAck() {
	if (!m_extraAuth && m_pClient->asyncEConnect())
        m_pClient->startApi();
}
//! [connectack]


//! [error]
void ClientBaseImp::error(int id, int errorCode, const std::string& errorString)
{
	m_errorCode=errorCode;
	logInfo( "Error. Id: %d, Code: %d, Msg: %s", id, errorCode, errorString.c_str());
}
//! [error]

//! [tickprice]
void ClientBaseImp::tickPrice( TickerId tickerId, TickType field, double price, const TickAttrib& attribs) {
	logInfo( "Tick Price. Ticker Id: %ld, Field: %d, Price: %g, CanAutoExecute: %d, PastLimit: %d, PreOpen: %d", tickerId, (int)field, price, attribs.canAutoExecute, attribs.pastLimit, attribs.preOpen);
}
//! [tickprice]

//! [ticksize]
void ClientBaseImp::tickSize( TickerId tickerId, TickType field, int size) {
	logInfo( "Tick Size. Ticker Id: %ld, Field: %d, Size: %d", tickerId, (int)field, size);
}
//! [ticksize]

//! [tickoptioncomputation]
void ClientBaseImp::tickOptionComputation( TickerId tickerId, TickType tickType, double impliedVol, double delta,
                                          double optPrice, double pvDividend,
                                          double gamma, double vega, double theta, double undPrice) {
	logInfo( "TickOptionComputation. Ticker Id: %ld, Type: %d, ImpliedVolatility: %g, Delta: %g, OptionPrice: %g, pvDividend: %g, Gamma: %g, Vega: %g, Theta: %g, Underlying Price: %g", tickerId, (int)tickType, impliedVol, delta, optPrice, pvDividend, gamma, vega, theta, undPrice);
}
//! [tickoptioncomputation]

//! [tickgeneric]
void ClientBaseImp::tickGeneric(TickerId tickerId, TickType tickType, double value) {
	logInfo( "Tick Generic. Ticker Id: %ld, Type: %d, Value: %g", tickerId, (int)tickType, value);
}
//! [tickgeneric]

//! [tickstring]
void ClientBaseImp::tickString(TickerId tickerId, TickType tickType, const std::string& value) {
	logInfo( "Tick String. Ticker Id: %ld, Type: %d, Value: %s", tickerId, (int)tickType, value.c_str());
}
//! [tickstring]

void ClientBaseImp::tickEFP(TickerId tickerId, TickType tickType, double basisPoints, const std::string& formattedBasisPoints,
                            double totalDividends, int holdDays, const std::string& futureLastTradeDate, double dividendImpact, double dividendsToLastTradeDate) {
	logInfo( "TickEFP. %ld, Type: %d, BasisPoints: %g, FormattedBasisPoints: %s, Total Dividends: %g, HoldDays: %d, Future Last Trade Date: %s, Dividend Impact: %g, Dividends To Last Trade Date: %g", tickerId, (int)tickType, basisPoints, formattedBasisPoints.c_str(), totalDividends, holdDays, futureLastTradeDate.c_str(), dividendImpact, dividendsToLastTradeDate);
}

//! [orderstatus]
void ClientBaseImp::orderStatus(OrderId orderId, const std::string& status, double filled,
		double remaining, double avgFillPrice, int permId, int parentId,
		double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice){
	logInfo("OrderStatus. Id: %ld, Status: %s, Filled: %g, Remaining: %g, AvgFillPrice: %g, PermId: %d, LastFillPrice: %g, ClientId: %d, WhyHeld: %s, MktCapPrice: %g", orderId, status.c_str(), filled, remaining, avgFillPrice, permId, lastFillPrice, clientId, whyHeld.c_str(), mktCapPrice);
}
//! [orderstatus]

//! [openorder]
void ClientBaseImp::openOrder( OrderId orderId, const Contract& contract, const Order& order, const OrderState& ostate) {
	logInfo( "OpenOrder. ID: %ld, %s, %s @ %s: %s, %s, %g, %g, %s, %s", orderId, contract.symbol.c_str(), contract.secType.c_str(), contract.exchange.c_str(), order.action.c_str(), order.orderType.c_str(), order.totalQuantity, order.cashQty == UNSET_DOUBLE ? 0 : order.cashQty, ostate.status.c_str(), order.dontUseAutoPriceForHedge ? "true" : "false");
	if (order.whatIf) {
		logInfo( "What-If. ID: %ld, InitMarginBefore: %s, MaintMarginBefore: %s, EquityWithLoanBefore: %s, InitMarginChange: %s, MaintMarginChange: %s, EquityWithLoanChange: %s, InitMarginAfter: %s, MaintMarginAfter: %s, EquityWithLoanAfter: %s",
			orderId, Utils::formatDoubleString(ostate.initMarginBefore).c_str(), Utils::formatDoubleString(ostate.maintMarginBefore).c_str(), Utils::formatDoubleString(ostate.equityWithLoanBefore).c_str(),
			Utils::formatDoubleString(ostate.initMarginChange).c_str(), Utils::formatDoubleString(ostate.maintMarginChange).c_str(), Utils::formatDoubleString(ostate.equityWithLoanChange).c_str(),
			Utils::formatDoubleString(ostate.initMarginAfter).c_str(), Utils::formatDoubleString(ostate.maintMarginAfter).c_str(), Utils::formatDoubleString(ostate.equityWithLoanAfter).c_str());
	}
}
//! [openorder]

//! [openorderend]
void ClientBaseImp::openOrderEnd() {
	logInfo( "OpenOrderEnd");
}
//! [openorderend]

void ClientBaseImp::winError( const std::string& str, int lastError) {}
void ClientBaseImp::connectionClosed() {
	logInfo( "Connection Closed");
}

//! [updateaccountvalue]
void ClientBaseImp::updateAccountValue(const std::string& key, const std::string& val,
                                       const std::string& currency, const std::string& accountName) {
	logInfo("UpdateAccountValue. Key: %s, Value: %s, Currency: %s, Account Name: %s", key.c_str(), val.c_str(), currency.c_str(), accountName.c_str());
}
//! [updateaccountvalue]

//! [updateportfolio]
void ClientBaseImp::updatePortfolio(const Contract& contract, double position,
                                    double marketPrice, double marketValue, double averageCost,
                                    double unrealizedPNL, double realizedPNL, const std::string& accountName){
	logInfo("UpdatePortfolio. %s, %s @ %s: Position: %g, MarketPrice: %g, MarketValue: %g, AverageCost: %g, UnrealizedPNL: %g, RealizedPNL: %g, AccountName: %s", (contract.symbol).c_str(), (contract.secType).c_str(), (contract.primaryExchange).c_str(), position, marketPrice, marketValue, averageCost, unrealizedPNL, realizedPNL, accountName.c_str());
}
//! [updateportfolio]

//! [updateaccounttime]
void ClientBaseImp::updateAccountTime(const std::string& timeStamp) {
	logInfo( "UpdateAccountTime. Time: %s", timeStamp.c_str());
}
//! [updateaccounttime]

//! [accountdownloadend]
void ClientBaseImp::accountDownloadEnd(const std::string& accountName) {
	logInfo( "Account download finished: %s", accountName.c_str());
}
//! [accountdownloadend]

//! [contractdetails]
void ClientBaseImp::contractDetails( int reqId, const ContractDetails& contractDetails) {
	logInfo( "ContractDetails begin. ReqId: %d", reqId);
	std::string cmsg=printContractMsg(contractDetails.contract);
	std::string dtl=printContractDetailsMsg(contractDetails);
	logInfo("%s\n%s",cmsg.c_str(), dtl.c_str());
	logInfo( "ContractDetails end. ReqId: %d", reqId);
}
//! [contractdetails]

//! [bondcontractdetails]
void ClientBaseImp::bondContractDetails( int reqId, const ContractDetails& contractDetails) {
	logInfo( "BondContractDetails begin. ReqId: %d", reqId);
	logInfo("%s", printBondContractDetailsMsg(contractDetails).c_str());
	logInfo( "BondContractDetails end. ReqId: %d", reqId);
}
//! [bondcontractdetails]

//! [contractdetailsend]
void ClientBaseImp::contractDetailsEnd( int reqId) {
	logInfo( "ContractDetailsEnd. %d", reqId);
}
//! [contractdetailsend]

//! [execdetails]
void ClientBaseImp::execDetails( int reqId, const Contract& contract, const Execution& execution) {
	logInfo( "ExecDetails. ReqId: %d - %s, %s, %s - %s, %ld, %g, %d", reqId, contract.symbol.c_str(), contract.secType.c_str(), contract.currency.c_str(), execution.execId.c_str(), execution.orderId, execution.shares, execution.lastLiquidity);
}
//! [execdetails]

//! [execdetailsend]
void ClientBaseImp::execDetailsEnd( int reqId) {
	logInfo( "ExecDetailsEnd. %d", reqId);
}
//! [execdetailsend]

//! [updatemktdepth]
void ClientBaseImp::updateMktDepth(TickerId id, int position, int operation, int side,
                                   double price, int size) {
	logInfo( "UpdateMarketDepth. %ld - Position: %d, Operation: %d, Side: %d, Price: %g, Size: %d", id, position, operation, side, price, size);
}
//! [updatemktdepth]

//! [updatemktdepthl2]
void ClientBaseImp::updateMktDepthL2(TickerId id, int position, const std::string& marketMaker, int operation,
                                     int side, double price, int size) {
	logInfo( "UpdateMarketDepthL2. %ld - Position: %d, Operation: %d, Side: %d, Price: %g, Size: %d", id, position, operation, side, price, size);
}
//! [updatemktdepthl2]

//! [updatenewsbulletin]
void ClientBaseImp::updateNewsBulletin(int msgId, int msgType, const std::string& newsMessage, const std::string& originExch) {
//	logInfo( "News Bulletins. %d - Type: %d, Message: %s, Exchange of Origin: %s", msgId, msgType, newsMessage.c_str(), originExch.c_str());
//	the message has problem
	logInfo( "News Bulletins. %d - Type: %d", msgId, msgType);
}
//! [updatenewsbulletin]

//! [managedaccounts]
void ClientBaseImp::managedAccounts( const std::string& accountsList) {
	logInfo( "Account List: %s", accountsList.c_str());
}
//! [managedaccounts]

//! [receivefa]
void ClientBaseImp::receiveFA(faDataType pFaDataType, const std::string& cxml) {
	logInfo("Receiving FA: %d\n %s", (int)pFaDataType, cxml.c_str());
}
//! [receivefa]

//! [historicaldata]
void ClientBaseImp::historicalData(TickerId reqId, const Bar& bar) {
	logInfo( "HistoricalData. ReqId: %ld - Date: %s, Open: %g, High: %g, Low: %g, Close: %g, Volume: %lld, Count: %d, WAP: %g", reqId, bar.time.c_str(), bar.open, bar.high, bar.low, bar.close, bar.volume, bar.count, bar.wap);
}
//! [historicaldata]

//! [historicaldataend]
void ClientBaseImp::historicalDataEnd(int reqId, const std::string& startDateStr, const std::string& endDateStr) {
	logInfo("HistoricalDataEnd. ReqId: %d - Start Date: %s End Date: %s", reqId, startDateStr.c_str(), endDateStr.c_str());
}
//! [historicaldataend]

//! [scannerparameters]
void ClientBaseImp::scannerParameters(const std::string& xml) {
	logInfo( "ScannerParameters. %s", xml.c_str());
}
//! [scannerparameters]

//! [scannerdata]
void ClientBaseImp::scannerData(int reqId, int rank, const ContractDetails& contractDetails,
                                const std::string& distance, const std::string& benchmark, const std::string& projection,
                                const std::string& legsStr) {
	logInfo( "ScannerData. %d - Rank: %d, Symbol: %s, SecType: %s, Currency: %s, Distance: %s, Benchmark: %s, Projection: %s, Legs String: %s", reqId, rank, contractDetails.contract.symbol.c_str(), contractDetails.contract.secType.c_str(), contractDetails.contract.currency.c_str(), distance.c_str(), benchmark.c_str(), projection.c_str(), legsStr.c_str());
}
//! [scannerdata]

//! [scannerdataend]
void ClientBaseImp::scannerDataEnd(int reqId) {
	logInfo( "ScannerDataEnd. %d", reqId);
}
//! [scannerdataend]

//! [realtimebar]
void ClientBaseImp::realtimeBar(TickerId reqId, long time, double open, double high, double low, double close,
                                long volume, double wap, int count) {
	logInfo( "RealTimeBars. %ld - Time: %ld, Open: %g, High: %g, Low: %g, Close: %g, Volume: %ld, Count: %d, WAP: %g", reqId, time, open, high, low, close, volume, count, wap);
}
//! [realtimebar]

//! [fundamentaldata]
void ClientBaseImp::fundamentalData(TickerId reqId, const std::string& data) {
	logInfo( "FundamentalData. ReqId: %ld, %s", reqId, data.c_str());
}
//! [fundamentaldata]

void ClientBaseImp::deltaNeutralValidation(int reqId, const DeltaNeutralContract& deltaNeutralContract) {
	logInfo( "DeltaNeutralValidation. %d, ConId: %ld, Delta: %g, Price: %g", reqId, deltaNeutralContract.conId, deltaNeutralContract.delta, deltaNeutralContract.price);
}

//! [ticksnapshotend]
void ClientBaseImp::tickSnapshotEnd(int reqId) {
	logInfo( "TickSnapshotEnd: %d", reqId);
}
//! [ticksnapshotend]

//! [marketdatatype]
void ClientBaseImp::marketDataType(TickerId reqId, int marketDataType) {
	logInfo( "MarketDataType. ReqId: %ld, Type: %d", reqId, marketDataType);
}
//! [marketdatatype]

//! [commissionreport]
void ClientBaseImp::commissionReport( const CommissionReport& commissionReport) {
	logInfo( "CommissionReport. %s - %g %s RPNL %g", commissionReport.execId.c_str(), commissionReport.commission, commissionReport.currency.c_str(), commissionReport.realizedPNL);
}
//! [commissionreport]

//! [position]
void ClientBaseImp::position( const std::string& account, const Contract& contract, double position, double avgCost) {
	logInfo( "Position. %s - Symbol: %s, SecType: %s, Currency: %s, Position: %g, Avg Cost: %g", account.c_str(), contract.symbol.c_str(), contract.secType.c_str(), contract.currency.c_str(), position, avgCost);
}
//! [position]

//! [positionend]
void ClientBaseImp::positionEnd() {
	logInfo( "PositionEnd");
}
//! [positionend]

//! [accountsummary]
void ClientBaseImp::accountSummary( int reqId, const std::string& account, const std::string& tag, const std::string& value, const std::string& currency) {
	logInfo( "Acct Summary. ReqId: %d, Account: %s, Tag: %s, Value: %s, Currency: %s", reqId, account.c_str(), tag.c_str(), value.c_str(), currency.c_str());
}
//! [accountsummary]

//! [accountsummaryend]
void ClientBaseImp::accountSummaryEnd( int reqId) {
	logInfo( "AccountSummaryEnd. Req Id: %d", reqId);
}
//! [accountsummaryend]

void ClientBaseImp::verifyMessageAPI( const std::string& apiData) {
	logInfo("verifyMessageAPI: %s\b", apiData.c_str());
}

void ClientBaseImp::verifyCompleted( bool isSuccessful, const std::string& errorText) {
	logInfo("verifyCompleted. IsSuccessfule: %d - Error: %s", isSuccessful, errorText.c_str());
}

void ClientBaseImp::verifyAndAuthMessageAPI( const std::string& apiDatai, const std::string& xyzChallenge) {
	logInfo("verifyAndAuthMessageAPI: %s %s", apiDatai.c_str(), xyzChallenge.c_str());
}

void ClientBaseImp::verifyAndAuthCompleted( bool isSuccessful, const std::string& errorText) {
	logInfo("verifyAndAuthCompleted. IsSuccessful: %d - Error: %s", isSuccessful, errorText.c_str());
    if (isSuccessful)
        m_pClient->startApi();
}

//! [displaygrouplist]
void ClientBaseImp::displayGroupList( int reqId, const std::string& groups) {
	logInfo("Display Group List. ReqId: %d, Groups: %s", reqId, groups.c_str());
}
//! [displaygrouplist]

//! [displaygroupupdated]
void ClientBaseImp::displayGroupUpdated( int reqId, const std::string& contractInfo) {
	logInfo("Display Group Updated. ReqId: %d, Contract Info: %s", (int)reqId, contractInfo.c_str());
}
//! [displaygroupupdated]

//! [positionmulti]
void ClientBaseImp::positionMulti( int reqId, const std::string& account,const std::string& modelCode, const Contract& contract, double pos, double avgCost) {
	logInfo("Position Multi. Request: %d, Account: %s, ModelCode: %s, Symbol: %s, SecType: %s, Currency: %s, Position: %g, Avg Cost: %g", reqId, account.c_str(), modelCode.c_str(), contract.symbol.c_str(), contract.secType.c_str(), contract.currency.c_str(), pos, avgCost);
}
//! [positionmulti]

//! [positionmultiend]
void ClientBaseImp::positionMultiEnd( int reqId) {
	logInfo("Position Multi End. Request: %d", reqId);
}
//! [positionmultiend]

//! [accountupdatemulti]
void ClientBaseImp::accountUpdateMulti( int reqId, const std::string& account, const std::string& modelCode, const std::string& key, const std::string& value, const std::string& currency) {
	logInfo("AccountUpdate Multi. Request: %d, Account: %s, ModelCode: %s, Key, %s, Value: %s, Currency: %s", reqId, account.c_str(), modelCode.c_str(), key.c_str(), value.c_str(), currency.c_str());
}
//! [accountupdatemulti]

//! [accountupdatemultiend]
void ClientBaseImp::accountUpdateMultiEnd( int reqId) {
	logInfo("Account Update Multi End. Request: %d", reqId);
}
//! [accountupdatemultiend]

//! [securityDefinitionOptionParameter]
void ClientBaseImp::securityDefinitionOptionalParameter(int reqId, const std::string& exchange, int underlyingConId, const std::string& tradingClass,
                                                        const std::string& multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes) {
	logInfo("Security Definition Optional Parameter. Request: %d, Trading Class: %s, Multiplier: %s", reqId, tradingClass.c_str(), multiplier.c_str());
}
//! [securityDefinitionOptionParameter]

//! [securityDefinitionOptionParameterEnd]
void ClientBaseImp::securityDefinitionOptionalParameterEnd(int reqId) {
	logInfo("Security Definition Optional Parameter End. Request: %d", reqId);
}
//! [securityDefinitionOptionParameterEnd]

//! [softDollarTiers]
void ClientBaseImp::softDollarTiers(int reqId, const std::vector<SoftDollarTier> &tiers) {
	logInfo("Soft dollar tiers (%lu):", tiers.size());

	for (unsigned int i = 0; i < tiers.size(); i++) {
		logInfo("%s", tiers[i].displayName().c_str());
	}
}
//! [softDollarTiers]

//! [familyCodes]
void ClientBaseImp::familyCodes(const std::vector<FamilyCode> &familyCodes) {
	logInfo("Family codes (%lu):", familyCodes.size());

	for (unsigned int i = 0; i < familyCodes.size(); i++) {
		logInfo("Family code [%d] - accountID: %s familyCodeStr: %s", i, familyCodes[i].accountID.c_str(), familyCodes[i].familyCodeStr.c_str());
	}
}
//! [familyCodes]

//! [symbolSamples]
void ClientBaseImp::symbolSamples(int reqId, const std::vector<ContractDescription> &contractDescriptions) {
	logInfo("Symbol Samples (total=%lu) reqId: %d", contractDescriptions.size(), reqId);

	for (unsigned int i = 0; i < contractDescriptions.size(); i++) {
		Contract contract = contractDescriptions[i].contract;
		std::vector<std::string> derivativeSecTypes = contractDescriptions[i].derivativeSecTypes;
		logInfo("Contract (%u): %ld %s %s %s %s, ", i, contract.conId, contract.symbol.c_str(), contract.secType.c_str(), contract.primaryExchange.c_str(), contract.currency.c_str());
		logInfo("Derivative Sec-types (%lu):", derivativeSecTypes.size());
		for (unsigned int j = 0; j < derivativeSecTypes.size(); j++) {
			logInfo(" %s", derivativeSecTypes[j].c_str());
		}
		logInfo("");
	}
}
//! [symbolSamples]

//! [mktDepthExchanges]
void ClientBaseImp::mktDepthExchanges(const std::vector<DepthMktDataDescription> &depthMktDataDescriptions) {
	logInfo("Mkt Depth Exchanges (%lu):", depthMktDataDescriptions.size());

	for (unsigned int i = 0; i < depthMktDataDescriptions.size(); i++) {
		logInfo("Depth Mkt Data Description [%d] - exchange: %s secType: %s listingExch: %s serviceDataType: %s aggGroup: %s", i,
			depthMktDataDescriptions[i].exchange.c_str(),
			depthMktDataDescriptions[i].secType.c_str(),
			depthMktDataDescriptions[i].listingExch.c_str(),
			depthMktDataDescriptions[i].serviceDataType.c_str(),
			depthMktDataDescriptions[i].aggGroup != INT_MAX ? std::to_string(depthMktDataDescriptions[i].aggGroup).c_str() : "");
	}
}
//! [mktDepthExchanges]

//! [tickNews]
void ClientBaseImp::tickNews(int tickerId, time_t timeStamp, const std::string& providerCode, const std::string& articleId, const std::string& headline, const std::string& extraData) {
	logInfo("News Tick. TickerId: %d, TimeStamp: %s, ProviderCode: %s, ArticleId: %s, Headline: %s, ExtraData: %s", tickerId, ctime(&(timeStamp /= 1000)), providerCode.c_str(), articleId.c_str(), headline.c_str(), extraData.c_str());
}
//! [tickNews]

//! [smartcomponents]]
void ClientBaseImp::smartComponents(int reqId, const SmartComponentsMap& theMap) {
	logInfo("Smart components: (%lu):", theMap.size());

	for (SmartComponentsMap::const_iterator i = theMap.begin(); i != theMap.end(); i++) {
		logInfo(" bit number: %d exchange: %s exchange letter: %c", i->first, std::get<0>(i->second).c_str(), std::get<1>(i->second));
	}
}
//! [smartcomponents]

//! [tickReqParams]
void ClientBaseImp::tickReqParams(int tickerId, double minTick, const std::string& bboExchange, int snapshotPermissions) {
	logInfo("tickerId: %d, minTick: %g, bboExchange: %s, snapshotPermissions: %u", tickerId, minTick, bboExchange.c_str(), snapshotPermissions);
}
//! [tickReqParams]

//! [newsProviders]
void ClientBaseImp::newsProviders(const std::vector<NewsProvider> &newsProviders) {
	logInfo("News providers (%lu):", newsProviders.size());

	for (unsigned int i = 0; i < newsProviders.size(); i++) {
		logInfo("News provider [%d] - providerCode: %s providerName: %s", i, newsProviders[i].providerCode.c_str(), newsProviders[i].providerName.c_str());
	}
}
//! [newsProviders]

//! [newsArticle]
void ClientBaseImp::newsArticle(int requestId, int articleType, const std::string& articleText) {
	logInfo("News Article. Request Id: %d, Article Type: %d", requestId, articleType);
	if (articleType == 0) {
		logInfo("News Article Text (text or html): %s", articleText.c_str());
	} else if (articleType == 1) {
		std::string path;
		#if defined(IB_WIN32)
			TCHAR s[200];
			GetCurrentDirectory(200, s);
			path = s + std::string("\\MST$06f53098.pdf");
		#elif defined(IB_POSIX)
			char s[1024];
			if (getcwd(s, sizeof(s)) == NULL) {
				logInfo("getcwd() error");
				return;
			}
			path = s + std::string("/MST$06f53098.pdf");
		#endif
		std::vector<std::uint8_t> bytes = Utils::base64_decode(articleText);
		FILE*fp = fopen(path.c_str(), "ab+");
		fwrite((const char*)bytes.data(), 1, bytes.size(), fp);
		fclose(fp);
		//std::ofstream outfile(path, std::ios::out | std::ios::binary);
		//outfile.write((const char*)bytes.data(), bytes.size());
		logInfo("Binary/pdf article was saved to: %s", path.c_str());
	}
}
//! [newsArticle]

//! [historicalNews]
void ClientBaseImp::historicalNews(int requestId, const std::string& time, const std::string& providerCode, const std::string& articleId, const std::string& headline) {
	logInfo("Historical News. RequestId: %d, Time: %s, ProviderCode: %s, ArticleId: %s, Headline: %s", requestId, time.c_str(), providerCode.c_str(), articleId.c_str(), headline.c_str());
}
//! [historicalNews]

//! [historicalNewsEnd]
void ClientBaseImp::historicalNewsEnd(int requestId, bool hasMore) {
	logInfo("Historical News End. RequestId: %d, HasMore: %s", requestId, (hasMore ? "true" : " false"));
}
//! [historicalNewsEnd]

//! [headTimestamp]
void ClientBaseImp::headTimestamp(int reqId, const std::string& headTimestamp) {
	logInfo( "Head time stamp. ReqId: %d - Head time stamp: %s,", reqId, headTimestamp.c_str());

}
//! [headTimestamp]

//! [histogramData]
void ClientBaseImp::histogramData(int reqId, const HistogramDataVector& data) {
	logInfo("Histogram. ReqId: %d, data length: %lu", reqId, data.size());

	for (auto item : data) {
		logInfo("\t price: %f, size: %lld", item.price, item.size);
	}
}
//! [histogramData]

//! [historicalDataUpdate]
void ClientBaseImp::historicalDataUpdate(TickerId reqId, const Bar& bar) {
	logInfo( "HistoricalDataUpdate. ReqId: %ld - Date: %s, Open: %g, High: %g, Low: %g, Close: %g, Volume: %lld, Count: %d, WAP: %g", reqId, bar.time.c_str(), bar.open, bar.high, bar.low, bar.close, bar.volume, bar.count, bar.wap);
}
//! [historicalDataUpdate]

//! [rerouteMktDataReq]
void ClientBaseImp::rerouteMktDataReq(int reqId, int conid, const std::string& exchange) {
	logInfo( "Re-route market data request. ReqId: %d, ConId: %d, Exchange: %s", reqId, conid, exchange.c_str());
}
//! [rerouteMktDataReq]

//! [rerouteMktDepthReq]
void ClientBaseImp::rerouteMktDepthReq(int reqId, int conid, const std::string& exchange) {
	logInfo( "Re-route market depth request. ReqId: %d, ConId: %d, Exchange: %s", reqId, conid, exchange.c_str());
}
//! [rerouteMktDepthReq]

//! [marketRule]
void ClientBaseImp::marketRule(int marketRuleId, const std::vector<PriceIncrement> &priceIncrements) {
	logInfo("Market Rule Id: %d", marketRuleId);
	for (unsigned int i = 0; i < priceIncrements.size(); i++) {
		logInfo("Low Edge: %g, Increment: %g", priceIncrements[i].lowEdge, priceIncrements[i].increment);
	}
}
//! [marketRule]

//! [pnl]
void ClientBaseImp::pnl(int reqId, double dailyPnL, double unrealizedPnL, double realizedPnL) {
	logInfo("PnL. ReqId: %d, daily PnL: %g, unrealized PnL: %g, realized PnL: %g", reqId, dailyPnL, unrealizedPnL, realizedPnL);
}
//! [pnl]

//! [pnlsingle]
void ClientBaseImp::pnlSingle(int reqId, int pos, double dailyPnL, double unrealizedPnL, double realizedPnL, double value) {
	logInfo("PnL Single. ReqId: %d, pos: %d, daily PnL: %g, unrealized PnL: %g, realized PnL: %g, value: %g", reqId, pos, dailyPnL, unrealizedPnL, realizedPnL, value);
}
//! [pnlsingle]

//! [historicalticks]
void ClientBaseImp::historicalTicks(int reqId, const std::vector<HistoricalTick>& ticks, bool done) {
    for (HistoricalTick tick : ticks) {
    	std::time_t t = tick.time;
    	//std::cout << "Historical tick. ReqId: " << reqId << ", time: " << ctime(&t) << ", price: "<< tick.price << ", size: " << tick.size << std::endl;
    	logInfo("Historical tick. ReqId: %d, time: %s, price: %f, size: %lld", reqId, ctime(&t), tick.price, tick.size);
    }
}
//! [historicalticks]

//! [historicalticksbidask]
void ClientBaseImp::historicalTicksBidAsk(int reqId, const std::vector<HistoricalTickBidAsk>& ticks, bool done) {
    for (HistoricalTickBidAsk tick : ticks) {
	std::time_t t = tick.time;
        //std::cout << "Historical tick bid/ask. ReqId: " << reqId << ", time: " << ctime(&t) << ", mask: " << tick.mask << ", price bid: "<< tick.priceBid <<
        //    ", price ask: "<< tick.priceAsk << ", size bid: " << tick.sizeBid << ", size ask: " << tick.sizeAsk << std::endl;
        logInfo("Historical tick bid/ask. ReqId: %d, time: %s, mask %d, price bid: %f, price ask %f, size bid %lld, size ask %lld",
        		reqId, ctime(&t), tick.mask, tick.priceBid, tick.priceAsk,tick.sizeBid,tick.sizeAsk);

    }
}
//! [historicalticksbidask]

//! [historicaltickslast]
void ClientBaseImp::historicalTicksLast(int reqId, const std::vector<HistoricalTickLast>& ticks, bool done) {
    for (HistoricalTickLast tick : ticks) {
	std::time_t t = tick.time;
        //std::cout << "Historical tick last. ReqId: " << reqId << ", time: " << ctime(&t) << ", mask: " << tick.mask << ", price: "<< tick.price <<
        //    ", size: " << tick.size << ", exchange: " << tick.exchange << ", special conditions: " << tick.specialConditions << std::endl;
        logInfo("Historical tick last, ReqId: %d, time %s, mask %d, price %f, size %lld, exchange %s special conditions %s",
        		reqId, ctime(&t),tick.mask, tick.price,tick.size,tick.exchange.c_str(),tick.specialConditions.c_str());
    }
}
//! [historicaltickslast]

//! [tickbytickalllast]
void ClientBaseImp::tickByTickAllLast(int reqId, int tickType, time_t time, double price, int size, const TickAttrib& attribs, const std::string& exchange, const std::string& specialConditions) {
    logInfo("Tick-By-Tick. ReqId: %d, TickType: %s, Time: %s, Price: %g, Size: %d, PastLimit: %d, Unreported: %d, Exchange: %s, SpecialConditions:%s",
        reqId, (tickType == 1 ? "Last" : "AllLast"), ctime(&time), price, size, attribs.pastLimit, attribs.unreported, exchange.c_str(), specialConditions.c_str());
}
//! [tickbytickalllast]

//! [tickbytickbidask]
void ClientBaseImp::tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice, int bidSize, int askSize, const TickAttrib& attribs) {
    logInfo("Tick-By-Tick. ReqId: %d, TickType: BidAsk, Time: %s, BidPrice: %g, AskPrice: %g, BidSize: %d, AskSize: %d, BidPastLow: %d, AskPastHigh: %d",
        reqId, ctime(&time), bidPrice, askPrice, bidSize, askSize, attribs.bidPastLow, attribs.askPastHigh);
}
//! [tickbytickbidask]

//! [tickbytickmidpoint]
void ClientBaseImp::tickByTickMidPoint(int reqId, time_t time, double midPoint) {
    logInfo("Tick-By-Tick. ReqId: %d, TickType: MidPoint, Time: %s, MidPoint: %g", reqId, ctime(&time), midPoint);
}
//! [tickbytickmidpoint]

void ClientBaseImp::nextValidId( OrderId orderId)
{
	logInfo("Next Valid Id: %ld", orderId);
	//m_orderId = orderId;
}

void ClientBaseImp::currentTime( long time)
{
	time_t t = ( time_t)time;
	struct tm * timeinfo = localtime ( &t);
	logInfo( "The current date/time is: %s", asctime( timeinfo));
}


