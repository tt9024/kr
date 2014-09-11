#include "bookL2.hpp"

using namespace tp;
using namespace utils;
using namespace std;

int main() {
    // Create a book q
    vector<BookConfig> bc;
    bc.push_back(*new BookConfig("IB", "EUR/USD"));
    //bc.push_back(*new BookConfig("IB", "OIL/CLV4"));

    typedef BookQ<ShmCircularBuffer> BookQType;
    BookQType bq("IBClient", false, &bc);  // create a read/write queue
    BookQType::Writer& book_writer(bq.theWriter());

    eSecurity sec1 = SecMappings::instance().getSecId("EUR/USD");
    BookQType::Reader* book_reader = bq.newReader();

    BookDepot myBook(sec1);  // this is the book seen on the reader's side

    // update
    book_writer.newPrice(sec1, 1.3, 1000000, 0, true, 100);
    book_writer.newPrice(sec1, 1.4, 1000000, 0, false, 101);

    book_reader->getLatestUpdate(myBook);
    logInfo("latest book dump: %s", myBook.toString().c_str());

    book_writer.newPrice(sec1, 1.2, 1000000, 1, true, 102);
    book_writer.delPrice(sec1, 0, true, 102);

    // should be 1.2 - 1.4
    book_reader->getLatestUpdate(myBook);
    logInfo("latest book dump %s", myBook.toString().c_str());

    book_writer.delPrice(sec1, 1, true, 102);  // error del a non existing level
    book_writer.updPrice(sec1, 1.5, 2000000, 0, false, 103);
    book_writer.newPrice(sec1, 1.6, 2000000, 1, false, 104);

    // should be 1.2 - 1.5, 1.6
    book_reader->getLatestUpdate(myBook);
    logInfo("latest book dump %s", myBook.toString().c_str());

   book_writer.delPrice(sec1, 0, true, 104);
   book_writer.updPrice(sec1, 0, 1.1, 2000000, true, 105); // error upd a non existing level

   // should be NONE - 1.5, 1.6
   book_reader->getLatestUpdate(myBook);
   logInfo("latest book dump %s", myBook.toString().c_str());

   book_writer.newPrice(sec1, 1.2, 1000000, 0, true, 106);
   book_writer.newPrice(sec1, 1.3, 1000000, 0, true, 107); // insert and move back
   book_writer.newPrice(sec1, 1.25, 1000000, 1, true, 108); // insert and move back
   book_writer.delPrice(sec1, 1, true, 108); // del a middle price back into

   // should be 1.2, 1.3 - 1.5, 1.6
   book_reader->getLatestUpdate(myBook);
   logInfo("latest book dump %s", myBook.toString().c_str());


   book_writer.updPrice(sec1, 1.3, 1000000, 0, true, 109);  // shouldn't update anything
   book_writer.newPrice(sec1, 1.25, 1000000, 1, true, 110);

   book_writer.newPrice(sec1, 1.14, 1000000, 3, true, 111);
   book_writer.newPrice(sec1, 1.15, 1000000, 3, true, 112);
   book_writer.newPrice(sec1, 1.16, 1000000, 3, true, 113);
   book_writer.newPrice(sec1, 1.17, 1000000, 3, true, 114);
   book_writer.newPrice(sec1, 1.18, 1000000, 3, true, 115);
   book_writer.newPrice(sec1, 1.19, 1000000, 3, true, 116);  // error too many new levels

   // should be 1.14 ~ 1.8, 1.2, 1.25, 1.3 - 1.5, 1.6
   book_reader->getLatestUpdate(myBook);
   logInfo("latest book dump %s", myBook.toString().c_str());

   book_writer.delPrice(sec1, 7, true, 117);
   book_writer.delPrice(sec1, 0, true, 118);
   book_writer.delPrice(sec1, 1, true, 119);
   book_writer.delPrice(sec1, 4, true, 120);

   // should be 1.16, 1.17, 1.8, 1.25 - 1.5, 1.6
   book_reader->getLatestUpdate(myBook);
   logInfo("latest book dump %s", myBook.toString().c_str());

   book_writer.updBBOPriceOnly(sec1, 1.35, true, 121);
   book_writer.updBBOSizeOnly(sec1, 1000000, true, 122); // this should not update
   book_writer.updBBO(sec1, 1.45, 2000000, false, 123);

   // should be 1.16, 1.17, 1.8, 1.35 - 1.45, 1.6
   book_reader->getLatestUpdate(myBook);
   logInfo("latest book dump %s", myBook.toString().c_str());

   book_writer.resetBook(sec1);
   book_writer.updBBO(sec1, 1.35, 1000000, true, 122);
   book_writer.updBBOPriceOnly(sec1, 1.5, false, 123);
   book_writer.updBBOSizeOnly(sec1, 1000000, false, 124);
   book_writer.updBBOPriceOnly(sec1, 1.5, false, 125);  // this should not update

   // should be 1.35 - 1.45, 1.5
   book_reader->getLatestUpdate(myBook);
   logInfo("latest book dump %s", myBook.toString().c_str());

   return 0;

}
