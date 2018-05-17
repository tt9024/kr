/*
 * IBClientBase.hpp
 *
 *  Created on: May 6, 2018
 *      Author: zfu
 */
#pragma once
#include "EWrapper.h"
#include "EReader.h"
#include "EClientSocket.h"
#include "EPosixClientSocketPlatform.h"
#define TWS_PORT 7496
#define IBG_PORT 4001

class ClientBaseImp : public EWrapper {
public:
	explicit ClientBaseImp(int to_milli = 1);
	virtual ~ClientBaseImp();

	bool connect(const char * host, unsigned int port, int clientId);
	void disconnect() const;
	bool isConnected() const;
	void reqMDL1(const char* symbol, int ticker_id);
	void reqMDL2(const char* symbol, int ticker_id, int numLevel=8);
	void reqMDTbT(const char* symbol, int ticker_id);

	virtual int processMessages();
public:
	// events, replace all
	// the pure virtual functions
	// virtual.
    #include "EWrapper_prototypes.h"

protected:
	EReaderOSSignal m_osSignal;
	EClientSocket * const m_pClient;
	EReader *m_pReader;
    bool m_extraAuth;
    int m_errorCode;
};

static inline int IBPortSwitch(int port) {
	switch(port) {
		case TWS_PORT: return IBG_PORT;
		case IBG_PORT: return TWS_PORT;
	}
	return port;
}
