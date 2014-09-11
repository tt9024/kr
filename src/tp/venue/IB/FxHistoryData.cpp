#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "PosixTestClient.h"

#define TWS_PORT 7496
#define IBG_PORT 4001

const unsigned MAX_ATTEMPTS = 50;
const unsigned SLEEP_TIME = 10;

const char* const all_symbols = "EUR/USD GBP/USD USD/JPY USD/CHF USD/CAD "
		          "NZD/USD AUD/USD CHF/JPY EUR/JPY GBP/JPY "
		          "NZD/JPY AUD/JPY CAD/JPY EUR/AUD EUR/CAD "
		          "EUR/CHF GBP/CHF EUR/GBP NZD/CHF GBP/NZD "
		          "GBP/CAD AUD/CAD CAD/CHF NZD/CAD ";

PosixTestClient *client;

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
	if (argc < 6) {
		printf("Usage: %s client_id symbol date barSize in seconds(5/30) outFileName(append+) client_port(\"IB\" [def] or \"TWS\")\n", argv[0]);
		return -1;
	}

    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
	    printf("\ncan't catch SIGINT\n");
	    return -1;
    }

	int clientId = atoi(argv[1]);
	client = new PosixTestClient();

	const char* symbol = argv[2];
	const char* date = argv[3];
	int barSize = atoi(argv[4]);
	const char* histFile = argv[5];
	if (!client->openHistoryFile(histFile)) {
		printf("cannot create history file %s\n", histFile);
		delete client;
		return -1;
	}

	int port = IBG_PORT;
	if (argc > 6) {
		if (strcmp("TWS", argv[6]) == 0) {
			port = TWS_PORT;
		}
	}
	printf("client %d trying to get data for %s, connecting... \n", clientId, argv[2]);
	client->connect( NULL, port, clientId);
	printf("requesting %s on %s, bar size is %dS, writing to %s\n",
			symbol, date, barSize, histFile);


	if (barSize == 30) {
		client->reqHistFx30S(symbol, date);
		while( client->isConnected()) {
			if (!client->sock_recv(10)) {
				printf("client read 1 day on %s!\n", date);
				break;
			}
		}
	}
	else if (barSize == 5) {
		for (int h = 0; h<24; h+= 2) {
			printf("client reading 2 hours on %s, h=%d\n", date, h);
			client->reqHistFx5S(symbol, date, h);
			while( client->isConnected()) {
				if (!client->sock_recv(10)) {
					break;
				}
			}
			while (!client->isConnected())
				client->connect( NULL, port, ++clientId);
			sleep(10);
		}
		printf("client read 1 day on %s!\n", date);
	}

	delete client;
	printf("client %d exit.\n", clientId);
	return 0;
}

