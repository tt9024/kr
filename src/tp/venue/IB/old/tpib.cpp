#include <sstream>
#include <string>
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "IBClient.hpp"
#include "tp.hpp"

using namespace tp;
using namespace utils;
using namespace std;

typedef BookQ<ShmCircularBuffer> IBBookQType;
typedef OrderQ<ShmCircularBuffer> IBOrderQType;
typedef FillQ<ShmCircularBuffer> IBEventQType;
typedef TPVenue<IBBookQType::Writer, IBOrderQType::Reader, IBEventQType::Writer, IBClient> TPIBClientType;

TPIBClientType* ibclient;
volatile bool user_stopped = false;

void sig_handler(int signo)
{
  if (signo == SIGINT) {
    logInfo("IBClient received SIGINT, exiting...");
    printf("IBClient received SIGINT, exiting...\n");
  }
  user_stopped = true;
  ibclient->stopClient();
}

int main() {
    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }

    // Create a book q
    vector<BookConfig> bc;
    {
        // getting the config for market data subscription
        stringstream ss(plcc_getString("IBClientSub"));
        string symbol;

        while (getline(ss, symbol, ',')) {
            // need to remove the space
            bc.push_back(*new BookConfig("IB", symbol));
            logDebug("IBClient read symbol to subscribe: %s", symbol.c_str());
        }
    }

    IBBookQType bq(IBClientBook, false, &bc);  // create a book queue to write
    IBOrderQType oq(IBClientOrder, true);      // open the order input queue to read

    int IBVenueID = plcc_getInt("IBVenueID");    // open FillEvent queue to write for each trader
    std::vector<IBEventQType*> traderq;
    std::vector<IBEventQType::Writer*> traderq_writer;
    {
        // getting the config for all trader's queue
        stringstream ss(plcc_getString("TraderFillQ"));
        string traderQ;

        while (getline(ss, traderQ, ',')) {
            IBEventQType* eq = new IBEventQType(traderQ.c_str(), false);
            traderq.push_back(eq);
            traderq_writer.push_back(eq->newWriter(IBVenueID));
            logInfo("IBClient created trader queue: %s", traderQ.c_str());
        }
    }

    // start the IB Client thread
    user_stopped = false;
    while (!user_stopped) {
        ibclient = new TPIBClientType("IBClient", &bq.theWriter(), oq.newReader(), traderq_writer);
        ibclient->waitClient();
        logInfo("IBClient ended.");
        printf("IBClient ended.\n");
        delete ibclient;
        ibclient = NULL;
    }

    printf("Done.\n");

    return 0;
}
