#include "bookL2.hpp"
#include "order.hpp"
#include "thread_utils.h"

#define IBClientBook "IBClientBook"
#define IBClientOrder "IBClientOrder"

namespace tp {

// starting the TP
template <typename BookWriter, typename OrderReader, typename EventWriter,
          template<class, class, class> class TPClient >
class TPVenue {
public:

    typedef TPClient<BookWriter, OrderReader, EventWriter> ClientType;
    // start the client
    TPVenue(const char* name, BookWriter* bwriter, OrderReader* oreader, std::vector<EventWriter*> ewriters) :
        _name(name)
    {
        _client = new ClientType(bwriter, oreader, ewriters);
        _client_thread = new utils::ThreadWrapper<ClientType>(*_client);
        _client_thread->run(NULL);
    }

    void stopClient() {
        _client_thread->stop();
    }

    void waitClient() {
        _client_thread->join();
    }

    ~TPVenue() {
        delete _client;
        _client = NULL;
        delete _client_thread;
        _client_thread = NULL;
    };

private:
    const std::string _name;
    ClientType* _client;
    utils::ThreadWrapper<ClientType>* _client_thread;
};

}
