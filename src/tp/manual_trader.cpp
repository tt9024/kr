#include <sstream>
#include <string>
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "tp.hpp"
#include "sys_utils.h"

using namespace tp;
using namespace utils;
using namespace std;

#define MANUAL_TRADER_ID 0
typedef BookQ<ShmCircularBuffer> BookQType;
typedef OrderQ<ShmCircularBuffer> OrderQType;
typedef FillQ<ShmCircularBuffer> FillQType;

void help() {
    printf("New Order: [B|S] SYM SZ PX([a|b][+|-]price)\n");
    printf("Cancel Order: C #OID\n");
    printf("Get Price: [P] SYM\n");
    printf("Exit: [X]\n");
}

BookDepot books[utils::TotalSecurity];
bool interrupted;
unsigned int oid;

bool makeOrder(const std::string &cmd, OrderQType::Writer* owriter) {
    // very messy string operations
    stringstream ss(cmd);

    bool is_buy;
    string token;
    string symbol;
    int size;
    Price price;

    getline(ss, token, ' ');
    switch (token.c_str()[0]) {
    case 'B' :
    case 'S' :
    {
        is_buy = (token.c_str()[0] == 'B');
        // make a new order out of this
        getline(ss, symbol, ' ');
        eSecurity secid = getSymbolID(symbol.c_str());
        getline(ss, token, ' ');
        size = atoi(token.c_str());
        if (size <= 0) {
            printf("wrong size %d!\n", size);
            break;
        }
        getline(ss, token, ' ');
        switch (token.c_str()[0]) {
        case 'b' :
        case 'a' :
        {
            bool is_bid = (token.c_str()[0] == 'b');
            PriceEntry* pe = &(books[(int)secid].pe[is_bid? 0:BookLevel]);
            PriceEntry* pe_end = pe+BookLevel;
            for (; pe < pe_end; ++pe) {
                if (pe->size >= size) {
                    price = pe->price;
                    break;
                }
            };
            if (pe >= pe_end) {
                printf("No price found on Bid side for size %d\n", size);
                return false;
            }
            price = (is_bid? price:-price )+ atoi(token.c_str() + 1);
            break;
        }
        default:
        {
            price = pxToInt(secid, atof(token.c_str()));
        }
        }
        printf("%s %d %s at %.7lf [Y/N]? (press return to confirm)\n",
                is_buy?"Buy":"Sell", size, symbol.c_str(), pxToDouble(secid, price));
        token.clear();
        getline (cin, token);
        if ((token.c_str()[0] != 'n') && (token.c_str()[0] != 'N')) {
            owriter->newOrder(++oid, secid, TIF_GTC, OT_Limit, is_buy?BS_Buy:BS_Sell, price, size);
            printf("new order sent, order id: %lu.\n", (unsigned long) oid);
        }
        break;
    }
    case 'C' :
    {
        // make a cancel out of this
        getline(ss, token, ' ');
        unsigned int ref_oid = atoi(token.c_str());
        printf("cancel order %d? (press return to confirm or Ctrl-C to exit\n",
                ref_oid);

        token.clear();
        getline (cin, token);
        if ((token.c_str()[0] != 'n') && (token.c_str()[0] != 'N')) {
            printf("canceling...\n");
            owriter->cancelOrder(++oid, ref_oid);
        }
        break;
    }
    case 'P' :
    {
        // print out the book depot
        getline(ss, token, ' ');
        try {
            eSecurity secid = getSymbolID(token.c_str());
            printf("%s\n", books[(int)secid].toString().c_str());
        } catch (...) {
            printf("unknown symbol: %s\n", token.c_str());
            break;
        }
        break;
    }
    case 'E':
        interrupted = true;
        break;
    default:
        printf("Unknow command %s\n", cmd.c_str());
        help();
        return false;
    }
    return true;
}


int main(int argc, char** argv) {
    BookQType bq(IBClientBook, true);
    OrderQType oq(IBClientOrder, false);
    FillQType eq("manual_trader", true);

    BookQType::Reader* breader = bq.newReader();
    OrderQType::Writer* owriter = oq.newWriter(MANUAL_TRADER_ID);
    FillQType::Reader* ereader = eq.newReader();

    BookDepot myBook;
    FillEvent fe;
    interrupted = false;
    oid = 10;

    while (!interrupted) {
        // read book update
        if (breader->getNextUpdate(myBook)) {
            books[(int)myBook.security_id] = myBook;
        }
        if (ereader->getNextUpdate(fe)) {
            printf("Execution Event: %s\n", fe.toString().c_str());
        }
        std::string cmd = kbhit();
        if (cmd.size()>0) {
            makeOrder(cmd, owriter);
        }
    }
    printf("Done.");
    return 0;
}
