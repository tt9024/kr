#include <bookL2.hpp>

#include <sstream>
#include <string>
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

using namespace tp;
using namespace utils;
using namespace std;

typedef BookQ<ShmCircularBuffer> BookQType;
volatile bool user_stopped = false;

void sig_handler(int signo)
{
  if (signo == SIGINT) {
    printf("Received SIGINT, exiting...\n");
  }
  user_stopped = true;
}

int main(int argc, char**argv) {
    if (argc != 2) {
        printf("Usage: %s symbol\n", argv[0]);
        return 0;
    }
    std::string symbol(argv[1]);
    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    BookQType bq("IBClientBook", true);
    BookQType::Reader* book_reader = bq.newReader();
    eSecurity sec1 = SecMappings::instance().getSecId(symbol.c_str());
    BookDepot myBook(sec1);

    //uint64_t start_tm = utils::TimeUtil::cur_time_micro();
    user_stopped = false;
    while (!user_stopped) {
        if (book_reader->getNextUpdate(myBook))
        {
            if (myBook.security_id == sec1)
                printf("%s\n", myBook.toString().c_str());
        }
    }
    delete book_reader;
    printf("Done.\n");
    return 0;
}
