#include <queue.h>
#include <ThreadUtils.h>
#include <iostream>
#include <stdio.h>

#define QLen 64*1024

using namespace utils;

struct Item {
	char stuff1[32];
	long long number;
	char stuff2[32];
};

template<typename QReaderType>
class QReader {
public:
	QReader(int id, bool ifcopy) : signaled(false), m_id(id), ifCopy(ifcopy) {};
	void run(void* param) {
		QReaderType* reader = (QReaderType*) param;
		long long number = 0;
		Item item;
		Item* pi = &item;
		while (!signaled) {
			QStatus qs;
			if (ifCopy)
				qs = reader->copyNextIn(pi);
			else
				qs = reader->takeNextPtr(pi);
			if(qs == QStat_OK) {
				if (pi->number != number + 1) {
					std::cout << m_id << " gap of " << pi->number - number << std::endl;
				}
				number = pi->number;
				reader->advance();
			}
		}
	}
	void stop() { signaled = true; };
	volatile bool signaled;
	int m_id;
	const bool ifCopy;
};

template<typename QWriterType>
class QWriter {
public:
	QWriter(int id, int ndelay) : signaled(false), m_id(id), nano_delay(ndelay) {};
	void run(void* param) {
		QWriterType* writer = (QWriterType*) param;
		long long number = 0;
		Item item;
		item.number = 1;
		struct timespec tspec;
		tspec.tv_sec = 0;
		tspec.tv_nsec = nano_delay;
		while (!signaled) {
			writer->putNoSpin(&item);
			nanosleep(CLOCK_REALTIME, &tspec);
		}
	}
	void stop() { signaled = true; };
	volatile bool signaled;
	int m_id;
	int nano_delay;
};

typedef SwQueue<QLen, sizeof(Item)> QType;
int main(int argc, char**argv) {
	if(argc != 4) {
		printf("Usage: %s NumReaders ReaderIfCopy(0/1) WriterSleepNano\n", argv[0]);
		return 1;
	}
	int num_readers = atoi(argv[1]);
	bool ifCopy = (atoi(argv[2]) == 0)?false:true;
	int wait_nano = atoi(argv[3]);

	QType queu;
	// start readers
	for (int i=0; i<num_readers; ++i) {
		QReader<QType::Reader> reader(i, ifCopy);
		ThreadWrapper<QReader<QType::Reader> > reader_thread(reader);
		reader_thread.run((void*) queu.newReader());
	}

	QWriter<QType::Writer> writer(0, wait_nano);
	ThreadWrapper<QWriter<QType::Writer> > writer_thread(writer);
	writer_thread.run((void*) &(queu.theWriter()));
	writer_thread.join();
	return 0;
}
