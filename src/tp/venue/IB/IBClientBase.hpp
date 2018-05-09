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

class ClientBaseImp : public EWrapper {
public:
	explicit ClientBaseImp(int to_milli = 1);
	virtual ~ClientBaseImp();

	bool connect(const char * host, unsigned int port, int clientId);
	void disconnect() const;
	bool isConnected() const;

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
