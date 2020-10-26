#pragma once
#include <Vulcan/Common/FixUtils/FixUtils.h>

namespace FixUtils {
	/// FixSession handles the control plane, leaves the data plane stuff to Composer/Parser,
	/// Publisher will be used to report session updates such as ([dis]connection) and executions
	/// Can be derived to modify behaviors such as sequence reset handling
	template<typename Publisher, typename Socket, typename Derived>
	class FixSessionBase
	{
	public:
		FixSessionBase(const CConfig& config, Publisher& publisher);
		virtual ~FixSessionBase();

		/// the following functions are called by Socket on the receiving thread
		virtual void onReceive(const void*buf, int size, bool* bIsDeleted = NULL);
		virtual void onError(int errorCode, const char* errorMsg);
		virtual void onClose();
		virtual void onConnect();

		/// the following functions are called by publisher on main thread
		void sessionConnect();
		void onOneSec() {};
		std::string getLineInfo() const;
		void decodeMessage(const char* msg, int size);

	protected:
		const CConfig &m_config;
		Publisher &m_publisher;
		unsigned int m_timeCount, m_seqOut, m_seqIn;
		bool m_socketConnected, m_socketConnecting, m_loggedIn, m_sessionUp;
	};

	template<typename Publisher, typename Socket, typename Derived>
	FixSessionBase<Publisher, Socket, Derived>::FixSessionBase(const CConfig& config, Publisher& publisher)
	: Socket(config, publisher.getSuperRecvSocket()),
	  m_config(config),
	  m_publisher(publisher),
	  m_timeCount(-1),
	  m_seqOut(0),
	  m_seqIn(0),
	  m_socketConnected(false),
	  m_socketConnecting(false),
	  m_loggedIn(false),
	  m_sessionUp(false)
	{
	};

	template<typename Publisher, typename Socket, typename Derived>
	FixSessionBase<Publisher, Socket, Derived>::~FixSessionBase()
	{
	};

	void onOneSec() {};

	void logAndSend(const char* msg, int size)
	{
#ifdef DEBUG_FIX_SESSION
		FastLog(m_logger, "===> %s\n", dumpFixMsg(msg, size).c_str());
#endif
		Socket::sendToSocket(msg, size);
	}

	void  decodeMessage(const char* msg, int size)
	{
#ifdef DEBUG_CURRENEX
		FastLog(m_logger, "<=== %s\n", dumpFixMsg(msg, size).c_str());
#endif
		// msg points to the header, without start charactor
		unsigned long msToday = CurrenexUtil::getTimeStamp(msg);
		char msgType = CurrenexUtil::getType(msg);
		switch(msgType)
		{

	}


};
