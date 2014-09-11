#include "IBClient.hpp"
#include "tp.hpp"

using namespace tp;
using namespace utils;
using namespace std;

int main() {
    // Create a book q
    vector<BookConfig> bc;
    bc.push_back(*new BookConfig("IB", "EUR/USD"));
    //bc.push_back(*new BookConfig("IB", "USD/JPY"));
    //bc.push_back(*new BookConfig("IB", "GBP/USD"));

    typedef BookQ<ShmCircularBuffer> BookQType;
    typedef OrderQ<ShmCircularBuffer> OrderQType;
    typedef FillQ<ShmCircularBuffer> EventQType;
    typedef TPVenue<typename BookQType::Writer, typename OrderQType::Reader, typename EventQType::Writer, IBClient> TPIBClientType;

    int IBVenueId = 0;
    int IBClientTestTraderQId = 0;

    // create the queues for TP
    BookQType bq("IBClientBook", false, &bc);  // create a read/write queue
    OrderQType oq("IBClientOrder", false);
    EventQType eq("IBClientTestFillQueue", false);

    // create the reader/writers for TP
    BookQType::Reader* book_reader = bq.newReader();
    EventQType::Writer* ewriter = eq.newWriter(IBVenueId);
    std::vector<EventQType::Writer*> vec_fillq;
    vec_fillq.push_back(ewriter);

    // start the IB Client TP thread
    TPIBClientType ibclient("IBClient", &bq.theWriter(), oq.newReader(), vec_fillq);

    // create Book Reader
    eSecurity sec1 = SecMappings::instance().getSecId("EUR/USD");
    BookDepot myBook(sec1);

    // Order Writer;
    OrderQType::Writer* order_writer = oq.newWriter(IBClientTestTraderQId);

    // Fill Reader
    EventQType::Reader* fill_reader = eq.newReader();
    FillEvent myFill;

    int oid = 10;
    uint64_t start_tm = utils::TimeUtil::cur_time_micro();
    uint64_t mdupdates = 0;
    while (1) {
        if (book_reader->getNextUpdate(myBook))
        {
            ++mdupdates;
            if ((mdupdates % 100) == 0) {
                logInfo("IBClient got %llu updates.", (unsigned long long) mdupdates);
                logInfo("latest book dump: %s", myBook.toString().c_str());
            }
        }
        // make a new order every 10 second
        uint64_t tm = utils::TimeUtil::cur_time_micro();
        if ((tm - start_tm) > 10000000) {
            Price px = myBook.pe[0].price;
            order_writer->newOrder(oid++, sec1, TIF_GTC, OT_Limit, BS_Sell, px, 150000);
            start_tm = tm;
        }

        if (fill_reader->getNextUpdate(myFill)) {
            logInfo("Got Event: %s", myFill.toString().c_str());
        }
    }

   return 0;

}
