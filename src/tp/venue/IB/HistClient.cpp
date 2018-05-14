/*
 * HistClient.cpp
 *
 *  Created on: May 6, 2018
 *      Author: zfu
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdexcept>
#include "IBClientBase.hpp"
#include "IBContract.hpp"
#include <memory.h>
#include <iostream>
#include <plcc/plcc.hpp>

#define TWS_PORT 7496
#define IBG_PORT 4001

class HistoryDataClient : public ClientBaseImp {
public:
	enum BARTYPE {
		SIZESTR,
		DURSTR
	};

	static inline const std::string str_by_barsize(int bsize, BARTYPE type ) {
		switch (bsize) {
		case 1 : return type==DURSTR? "1800 S":  "1 secs";
		case 5 : return type==DURSTR? "3600 S":  "5 secs";
		case 10: return type==DURSTR? "14400 S": "10 secs";
		case 30: return type==DURSTR? "28800 S": "30 secs";
		case 60: return type==DURSTR? "1 D":     "1 min";
		case 300: return type==DURSTR? "1 D":     "5 mins";
		default :
			throw std::runtime_error("unknown bar size");
		}
	}

	explicit HistoryDataClient(int clientid, const char* symbol,
			const char* histFile, bool istrade,
			int bsize)
	    : ClientBaseImp(2000),  // don't have much to do other than process incoming
		  tid(clientid*10000), is_trade(istrade),
		  barsize(bsize), sz_str(str_by_barsize(bsize, SIZESTR)),
		  dur_str(str_by_barsize(bsize, DURSTR)),
		  type_str(is_trade? "TRADES":"MIDPOINT"),
		  historyFile(histFile), client_id(clientid),
		  received(0), fp(fopen(histFile,"a+"))
	{
		RicContract::get().makeContract(con, symbol,NULL);
		if (!fp) {
			throw std::runtime_error("file open failed");
		}
		printf("started -- %s\n", toString().c_str());
	};

	void reqHistBar(const char*end_date) {
		m_pClient->reqHistoricalData(++tid, con,  std::string(end_date),
				dur_str, sz_str, type_str, 0,2,false,TagValueListSPtr());
	}

	void reqHistTick(const char* end_date) {
		m_pClient->reqHistoricalTicks(++tid, con, "", std::string(end_date), 1000, "BID_ASK", 0, false,TagValueListSPtr());
	}
	void reqHistTickTrade(const char* end_date) {
		m_pClient->reqHistoricalTicks(++tid, con, "", std::string(end_date), 1000, "TRADES", 0, false,TagValueListSPtr());
	}

	~HistoryDataClient() {
		if (fp) fclose(fp);
	}
	void historicalData(TickerId reqId, const std::string& date, double open, double high,
						double low, double close, int volume, int barCount, double WAP, int hasGaps) {
		//FunctionPrint("%ld, %s, %lf, %lf, %lf, %lf, %d, %d, %lf, %d", reqId, date.c_str(), open, high, low,
		//		close, volume, barCount, WAP, hasGaps);
		if (strncmp(date.c_str(), "finished", 8)!=0) {
			fprintf(fp,"%s, %lf, %lf, %lf, %lf, %d, %d, %lf\n", date.c_str(), open, high, low,
					close, volume, barCount, WAP);
			fflush(fp);
			received++;
		}
	}

	//! [historicaldata] new API
	void historicalData(TickerId reqId, const Bar& bar) {
		historicalData(reqId, bar.time.c_str(), bar.open, bar.high, bar.low, bar.close, bar.volume, bar.count, bar.wap, 0);
		//logInfo( "HistoricalData. ReqId: %ld - Date: %s, Open: %g, High: %g, Low: %g, Close: %g, Volume: %lld, Count: %d, WAP: %g\n",
		//		reqId, bar.time.c_str(), bar.open, bar.high, bar.low, bar.close, bar.volume, bar.count, bar.wap);
	}

	//! [historicaltickslast]
	void historicalTicksLast(int reqId, const std::vector<HistoricalTickLast>& ticks, bool done) {
	    for (HistoricalTickLast tick : ticks) {
	    	//std::time_t t = tick.time;
	        //std::cout << "Historical tick last. ReqId: " << reqId << ", time: " << ctime(&t) << ", mask: " << tick.mask << ", price: "<< tick.price <<
	        //    ", size: " << tick.size << ", exchange: " << tick.exchange << ", special conditions: " << tick.specialConditions << std::endl;
	        //logInfo("Historical tick last, ReqId: %d, time %s, mask %d, price %f, size %lld, exchange %s special conditions %s",
	        //		reqId, ctime(&t),tick.mask, tick.price,tick.size,tick.exchange.c_str(),tick.specialConditions.c_str());
	    	fprintf(fp,"%lld, %lf, %lld, %d\n", tick.time, tick.price, tick.size, tick.mask);
	    	received++;
	    }
	    fflush(fp);
	}
	//! [historicalticks]
	void historicalTicks(int reqId, const std::vector<HistoricalTick>& ticks, bool done) {
	    for (const HistoricalTick& tick : ticks) {
	    	fprintf(fp,"%lld, %lf\n", tick.time, tick.price);
		    //	std::time_t t = tick.time;
		    //	localtime(&t);
		    //	std::cout << "Historical tick. ReqId: " << reqId << ", time: " << ctime(&t) << ", price: "<< tick.price << ", size: " << tick.size << std::endl;
		    	//logInfo("Historical tick. ReqId: %d, time: %s, price: %f, size: %lld", reqId, ctime(&t), tick.price, tick.size);
	    	received++;
	    }
		fflush(fp);
	}
	//! [historicalticks]

	//! [historicalticksbidask]
	void historicalTicksBidAsk(int reqId, const std::vector<HistoricalTickBidAsk>& ticks, bool done) {
	    for (HistoricalTickBidAsk tick : ticks) {
		    //std::time_t t = tick.time;
	        //std::cout << "Historical tick bid/ask. ReqId: " << reqId << ", time: " << ctime(&t) << ", mask: " << tick.mask << ", price bid: "<< tick.priceBid <<
	        //    ", price ask: "<< tick.priceAsk << ", size bid: " << tick.sizeBid << ", size ask: " << tick.sizeAsk << std::endl;
	        //logInfo("Historical tick bid/ask. ReqId: %d, time: %s, mask %d, price bid: %f, price ask %f, size bid %lld, size ask %lld",
	        //		reqId, ctime(&t), tick.mask, tick.priceBid, tick.priceAsk,tick.sizeBid,tick.sizeAsk);

	    	fprintf(fp,"%lld,%lf,%lld,%lf,%lld\n",tick.time,tick.priceBid,tick.sizeBid,tick.priceAsk,tick.sizeAsk);
	        received++;
	    }
	    fflush(fp);
	}
	//! [historicalticksbidask]

	std::string toString() const {
		char buf[512];
		snprintf(buf, sizeof(buf), "HistoryDataClient(%d/%d): histFile(%s) istrade(%s %s) bar_size_dur(%d, %s, %s) con(%s)",
				client_id,tid, historyFile.c_str(), is_trade?"True":"False", type_str.c_str(), barsize,
						sz_str.c_str(), dur_str.c_str(),printContractMsg(con).c_str());
		return std::string(buf);
	}

	int getReceived() const {
		return received;
	};

	void error(int id, int errorCode, const std::string& errorString)
	{
		logInfo( "Error. Id: %d, Code: %d, Msg: %s", id, errorCode, errorString.c_str());
		m_errorCode=errorCode;
		if (m_errorCode==162) { // no historical data returned
			logInfo("received error code %d, mark received",m_errorCode);
			received += 1;
		}
	}

private:
	int tid;
	const bool is_trade;
	const int barsize;
	const std::string sz_str;
	const std::string dur_str;
	const std::string type_str;
	Contract con;
	const std::string historyFile;
	const int client_id;
	volatile int received;
	FILE *fp;
};

HistoryDataClient *client;

void sig_handler(int signo)
{
  if (signo == SIGINT)
    printf("received SIGINT, exiting...\n");

  delete client;
  exit (1);
}
// Current function - real time data
// Fixme - work for 5/15/30 sec bar
int main(int argc, char** argv)
{
	if (argc < 7) {
		printf("Usage: %s client_id(100) symbol(NYM/CLM8) end_date(YYYYMMDD HH:MM:SS GMT) "
				"bar_sec(5) filename is_trade(0/1) client_port(\"IB\" [def] or \"TWS\")\n",
				argv[0]);
		return -1;
	}

    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
	    printf("\ncan't catch SIGINT\n");
	    return -1;
    }

	int clientId = atoi(argv[1]);

	const char* symbol = argv[2];
	const char* date = argv[3];
	int barSize = atoi(argv[4]);
	const char* histFile = argv[5];
	bool is_trade = argv[6][0]=='1';
	int port = IBG_PORT;
	if (argc > 7) {
		if (strcmp("TWS", argv[6]) == 0) {
			port = TWS_PORT;
		}
	}
	client = new HistoryDataClient(clientId,symbol, histFile, is_trade, barSize);
	bool received=false;
	int retrycnt=0;
	while( !received ) {
		printf("client %d trying to get data for %s, connecting to port %d... \n", clientId, symbol, port);
		client->connect( "127.0.0.1", port, clientId);
		if (client->isConnected()) {
			printf("requesting %s on %s, bar size is %dS, writing to %s\n",
					symbol, date, barSize, histFile);
		    //client->reqHistBar(date);
		    client->reqHistBar(date);
		    int cnt = 0;
			while (client->isConnected() && cnt < 5) {
				client->processMessages();
				if (int cnt = client->getReceived()) {
					printf("received %d updates\n", cnt);
					received=true;
					break;
				}
				++cnt;
			}
		}
		if (received) break;
		delete client;
		client = new HistoryDataClient(clientId,symbol, histFile, is_trade, barSize);
		// switch a port
		if (port == IBG_PORT) {
			port = TWS_PORT;
		} else {
			port = IBG_PORT;
		}
		printf("switching to port %d...\n", port);
		if (++retrycnt > 1) {
			printf("wait and retry...\n");
			sleep(10);
		}
	}
	delete client;
	printf("client %d exit.\n", clientId);
	return 0;
}




