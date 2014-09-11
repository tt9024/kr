#pragma once

#include "PLCC.hpp"
#include <vector>

struct ModelSignal {
    std::string symbol;
    bool isBuy;
    double price;
    int volume;
    int holdTimeSec;
};

// IO defines a non-blocking device:
// int IO.write(char* buf, int buflen)
// int IO.read(char* buf, int buflen)

template<typename IO, typename TRADER>
class ModelClient {
public:
    ModelClient(IO& io, TRADER& trader):
        m_io(io), m_trader(trader) {
        m_trader.plcc.logInfo("ModelClient started");
    }
    bool poll(ModelSignal& signal) {
        int recv_bytes = m_io.read(m_buffer, sizeof(m_buffer));
        std::vector<ModelSignal*> signals;
        int read_bytes = 0;
        while (read_bytes < recv_bytes) {
            read_bytes += makeSignal(m_buffer+read_bytes, recv_bytes-read_bytes, signals);
        }
        if (signals.size()) {
            m_trader.onSignal(signals);
            for (size_t i = 0; i<signals.size(); ++i) {
                delete signals[i];
            }
        }
    }

private:
    static const int BUFFER_LEN = 512;
    IO& m_io;
    TRADER& m_trader;
    char m_buffer[BUFFER_LEN];
};
