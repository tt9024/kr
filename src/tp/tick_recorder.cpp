#include <bookL2.hpp>

#include <sstream>
#include <string>
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>

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
    if (signal(SIGINT, sig_handler) == SIG_ERR)
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    if (argc != 2) {
        printf("Usage: %s Path_To_Repo\n", argv[0]);
        return 0;
    }
    string path (argv[1]);

    FILE* fps[utils::TotalSecurity];
    memset(fps, 0, sizeof(fps));

    BookQType bq("IBClientBook", true);
    BookQType::Reader* book_reader = bq.newReader();
    BookDepot myBook;

    //uint64_t start_tm = utils::TimeUtil::cur_time_micro();
    user_stopped = false;
    while (!user_stopped) {
        if (book_reader->getNextUpdate(myBook))
        {
            int secid = myBook.security_id;
            if (fps[secid] == NULL) {
                string file_path = path;
                string sym = getSymbol(secid);
                sym.erase(std::remove(sym.begin(), sym.end(), '/'), sym.end());
                file_path.append("/").append(sym);
                fps[secid] = fopen( file_path.c_str(), "a+");
                if (fps[secid] == NULL) {
                    printf("cannot create file for id %d: %s\n", (int)secid, file_path.c_str());
                    continue;
                }
            }
            fwrite(&myBook, sizeof(BookDepot), 1, fps[secid]);
            fflush(fps[secid]);
        }
    }
    for (int i = 0; i< (int)utils::TotalSecurity; ++i) {
        if (fps[i]) {
            fclose(fps[i]);
            fps[i] = 0;
        }
    }
    delete book_reader;
    printf("Done.\n");
    return 0;
}
