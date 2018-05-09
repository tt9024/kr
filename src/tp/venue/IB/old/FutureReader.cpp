#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "time_util.h"
#include "PosixTestClient.h"
#include <stdexcept>

#define TWS_PORT 4001

const unsigned MAX_ATTEMPTS = 50;
const unsigned SLEEP_TIME = 10;


class RealtimeMarketDataRecorder : public PosixTestClient {
public:
	struct PriceEntry {
		double price;
		int size;
		PriceEntry() : price(0), size(0) {};
		PriceEntry(double p, int s) : price(p), size(s) {};
		void reset() {
			price = 0; size = 0;
		}
		void set(double p, int s) {
			price = p; size = s;
		}
	};

	void updateMktDepth(TickerId id, int position, int operation, int side,
										  double price, int size) {
		// write to file
		fprintf(m_file[id - 1], "%llu %d %d %d %d %.5lf %d\n",
				TimeUtil::cur_time_gmt_micro(), (int)(id - 1), position, operation, side, price, size);
		fflush(m_file[id-1]);
	}

	void tickPrice(TickerId id, TickType field, double price, int canAutoExecute) {
		if ((field < 1) || (field > 2)) return;
		PriceEntry *pe = &(m_price[id-1][field-1]);
		pe->price = price;
		fprintf(m_file[id - 1], "%llu %d %d %d %d %.5lf %d\n",
				TimeUtil::cur_time_gmt_micro(), (int)(id - 1), 0, 1, (field==1)? 1:0, price, pe->size);
		fflush(m_file[id-1]);
		//PosixTestClient::tickPrice( id, field, price, canAutoExecute);
	}

	void tickSize(TickerId id, TickType field, int size) {
		if ((field != 0) && (field != 3)) return;
		PriceEntry *pe = &(m_price[id-1][field/3]);
		pe->size = size;
		fprintf(m_file[id - 1], "%llu %d %d %d %d %.5lf %d\n",
				TimeUtil::cur_time_gmt_micro(), (int)(id - 1), 0, 1, (field==0)? 1:0, pe->price, size);
		fflush(m_file[id-1]);
		//PosixTestClient::tickSize( id, field, size);
	}

	RealtimeMarketDataRecorder(int numSymbols, const char** symbols) : m_numSymbols(numSymbols), m_symbols(symbols) {
		char strbuf[32];
		if (numSymbols > MaxNumSymbols) {
			throw std::invalid_argument("too many symbols!");
		}
		for (int i=0; i<numSymbols; ++i) {
			m_file[i] = NULL;
			m_file[i] = fopen(getFileName(symbols[i], strbuf), "at+");
		}
	}

	~RealtimeMarketDataRecorder() {
		for (int i=0; i<MaxNumSymbols; ++i) {
			if (m_file[i]) {
				fclose(m_file[i]);
				m_file[i] = NULL;
			}
		}
	}
private:
	int m_numSymbols;
	const char** m_symbols;
	FILE* m_file[MaxNumSymbols];
	PriceEntry m_price[MaxNumSymbols][2];

	char* getFileName(const char* symbol, char* fname) {
		strcpy(fname, "dob_");
		char* ptr = fname + 4;
		while (*symbol) {
			if (*symbol != '/') {
				*ptr++ = *symbol++ ;
			} else {
				++symbol;
			}
		}
		*ptr = '\0';
		strcat(fname, ".log");
		return fname;
	}
};

PosixTestClient *client;

void sig_handler(int signo)
{
  if (signo == SIGINT)
    printf("received SIGINT, exiting...\n");

  delete client;
  exit (1);
}
// Current function - real time data
int main(int argc, const char** argv)
{
	if (argc < 4) {
		printf("Usage: %s client_id BookLevel symbol(space separated or \"ALL\")\n", argv[0]);
		return -1;
	}

    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
	    printf("\ncan't catch SIGINT\n");
	    return -1;
    }

	int clientId = atoi(argv[1]);
	int level = atoi(argv[2]);
	if (level < 1) {
		printf("level has to greater than 1: %d", level);
		return -1;
	}

	int numSymbols = argc - 3;
	const char** symbols = &(argv[3]);
	if (strcmp(argv[3], "ALL") == 0)  {
		symbols = all_symbols;
		numSymbols = MaxNumSymbols;
	}

	client = new RealtimeMarketDataRecorder(numSymbols, symbols);
	printf("client %d trying to get data for %s, connecting... \n", clientId, argv[2]);
	client->connect( NULL, TWS_PORT, clientId);
	printf("client %d trying to get data for %s, connected!\n", clientId, argv[2]);
	for (int i = 0; i<numSymbols; ++i) {
		printf("subscribing to %s\n", symbols[i]);
		if ((level == 1) || (i >= 3))  // Max 3 DoB subscriptions
			client->reqMDFx(symbols[i]);
		else
			client->reqMDFxDoB(symbols[i], level);
	}
	printf("\n");
	while( client->isConnected()) {
		client->sock_recv();
	}

	printf("client %d trying to get data for %s, exit.\n", clientId, argv[2]);
	return 0;
}
