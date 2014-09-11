#include "qtest.h"

typedef SwQueue<QLEN, DATALEN, ShmCircularBuffer> QType;

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s read[0/1], num_readers|writer_wait(micro), init_to_zero[0/1]\n", argv[0]);
        return 0;
    }

    bool is_read = (atoi(argv[1]) == 1);
    int param1 = atoi(argv[2]);
    bool init_to_zero = (argc>3)?(atoi(argv[3]) == 1) : false;

    int num_readers = param1;
    int wait_micro = param1;

    int itemCount = ITEMCNT;

    QType queue("qtest", is_read, init_to_zero);

    if (is_read) {
        for (int i=0; i<num_readers; ++i)
        {
            QueueReader< QType::Reader >* reader =
                    new QueueReader< QType::Reader >(i, *queue.newReader());
            ThreadWrapper< QueueReader< QType::Reader > >* readerThread =
                    new ThreadWrapper< QueueReader< QType::Reader > >(*reader);

            readerThread->run(&itemCount);
        }
        while (1) {
            sleep (1000000);
        }
    }


    QueueWriter< QType::Writer > writer (0, queue.theWriter(), wait_micro);
    ThreadWrapper< QueueWriter< QType::Writer > > writerThread(writer);
    writerThread.run(&itemCount);
    writerThread.join();
    return 0;
}
