#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "PosixTestClient.h"
#include "EclientSocketBase.h"
#include "EPosixClientSocket.h"
#include "sdk/shared/Contract.h"
#include <stdexcept>

#define TWS_PORT 7496
#define IBG_PORT 4001

const unsigned MAX_ATTEMPTS = 50;
const unsigned SLEEP_TIME = 10;

const char* const all_symbols = "EUR/USD GBP/USD USD/JPY USD/CHF USD/CAD "
		          "NZD/USD AUD/USD CHF/JPY EUR/JPY GBP/JPY "
		          "NZD/JPY AUD/JPY CAD/JPY EUR/AUD EUR/CAD "
		          "EUR/CHF GBP/CHF EUR/GBP NZD/CHF GBP/NZD "
		          "GBP/CAD AUD/CAD CAD/CHF NZD/CAD ";

class HistoryDataClient : public PosixTestClient {
public:
	enum BARTYPE {
		SIZESTR,
		DURSTR
	};

	static inline const IBString str_by_barsize(int bsize, BARTYPE type ) {
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
	    : tid(clientid*10000), is_trade(istrade),
		  barsize(bsize), sz_str(str_by_barsize(bsize, SIZESTR)),
		  dur_str(str_by_barsize(bsize, DURSTR)),
		  type_str(is_trade? "TRADES":"MIDPOINT"),
		  historyFile(histFile), client_id(clientid),
		  received(0), fp(fopen(histFile,"a+"))
	{
		makeContract(con, symbol,NULL);
		if (!fp) {
			throw std::runtime_error("file open failed");
		}
		printf("started -- %s\n", toString().c_str());
	};

	void reqHistBar(const char*end_date) {
		m_pClient->reqHistoricalData(++tid, con,  IBString(end_date),
				dur_str, sz_str, type_str, 0,2);

	}
	~HistoryDataClient() {
		if (fp) fclose(fp);
	}
	void historicalData(TickerId reqId, const IBString& date, double open, double high,
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

	std::string toString() const {
		char buf[512];
		snprintf(buf, sizeof(buf), "HistoryDataClient(%d/%d): histFile(%s) istrade(%s %s) bar_size_dur(%d, %s, %s) con(%s)",
				client_id,tid, historyFile.c_str(), is_trade?"True":"False", type_str.c_str(), barsize,
						sz_str.c_str(), dur_str.c_str(),con.ToString().c_str());
		return std::string(buf);
	}

	int getReceived() const { return received; };

private:
	int tid;
	const bool is_trade;
	const int barsize;
	const IBString sz_str;
	const IBString dur_str;
	const IBString type_str;
	Contract con;
	const IBString historyFile;
	const int client_id;
	int received;
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
		client->connect( NULL, port, clientId);
		if (client->isConnected()) {
			printf("requesting %s on %s, bar size is %dS, writing to %s\n",
					symbol, date, barSize, histFile);
		    client->reqHistBar(date);
			while (client->isConnected() && client->sock_recv(10)) {
				if (int cnt = client->getReceived()) {
					printf("received %d updates\n", cnt);
					received=true;
					break;
				}
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

