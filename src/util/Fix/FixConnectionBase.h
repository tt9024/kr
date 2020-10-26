#pragma once
#include <Vulcan/Common/FixUtils/FixMessageBuilder.h>
#include <Vulcan/Common/FixUtils/FixMessageReader.h>

namespace fixutils {
	//////////////////////////////
	// the sequence number stuff
	/////////////////////////////
	enum SeqCheckResponse {
		SeqCheckOK,
		SeqCheckDisconnect,
		SeqCheckskip
	};

	/*
	 * This is the base class that handles FIX session's control plane. Goal is to include
	 * most widely adopted connection behaviors, including connection, login/logout, testReq/HB.
	 * It also handles sequence reset and resend requests, save/retrieve sequence numbers
	 * to/from log files as needed.
	 * To implement venue can have specific behavior, create a derived class as the followings
	template<typename Publisher, typename Session, typename Socket>
	class FixConnectionIntegral : public FixConnectionBase<Publisher, Session, Socket, FixConnectionIntegral<Publisher, Session, Socket> >
	{
	public:
		unsigned int sendLogin()
		{
			// add additional tags
		}
	};
	* the data plane handler, i.e. Session, should implement
	* 		bool parse()
	* to handle data messages leave any un-handled messages the connection by return false
	* Call sessionConnect() to start connecting
	* onOneSec() needs to be called once per second to maintain connection states
	* The publisher needs to implement the following functions:
	* m_publisher.logInfo()
	* m_publisher.logAlert()
	* m_publisher.getLogFileName() - which gets the name of the log file
	* m_publisher.onClose() - called fix connection is closed
	* m_publisher.onConnect() - called when fix connection is connected
	*
	* m_session() is a passive object, which does parsing of the fix message
	* and publishes with m_publisher's interface
	*/
	template<typename Derived, typename Publisher, typename Session, typename Socket>
	class FixConnectionBase : public Socket
	{
	public:
		explicit FixConnectionBase(const CConfig& config, Publisher& publisher, Session& session, int inSeq = -1, int outSeq = -1);
		virtual ~FixConnectionBase();

		/// the following functions are called by Socket on the receiving thread
		virtual void onReceive(const void*buf, int size, bool* bIsDeleted = NULL);
		virtual void onError(int errorCode, const char* errorMsg);
		virtual void onClose();
		virtual void onConnect();
		void sessionConnect(); // start TCP connection
		void getCurSeqNumber(int& seqIn, int& seqOut) { seqIn = m_seqIn; seqOut = m_seqOut; };
		void onOneSec();
		std::string getLineInfo() const;
		const char* getUserName() const { return FixUtilConfig::get_username(config).c_str(); };

		/// the following functions are called by FixSessions for composition
		template<typename BuilderType>
		BuilderType& makeHeader(BuilderType& builder, const char* type, unsigned int seq);
		FixMessageBuilder& getWriter() const { return m_writer; };

	protected:
		// accessible by venue specific connection managers
		// behavior can be over-written:
		template<typename BuilderType>
		BuilderType& writeAdditionalHeaderField(BuilderType& builder) { return m_writer; };
		void sendSessionReq() {};

		// specify which day (0 - 6, Sunday - Saturday), sequence should reset
		// defaults to no reset on any weekday
		// together with getSequenceResetHour()
		int getSequenceResetDay() const { return -1 ; };

		// specify which hour (int local time), 0 - 23
		// defaults to no reset on any hour
		// together with getSequenceResetDay()
		int getSequenceResetHour() const { return -1; };

		// parse tag 58 and set m_seqOut accordingly
		// m_seqOut is the highest seq seen so far
		void parseSeqFromLogout() {};

		void prepareHeaderStr() {
			const std::string senderComp, targetComp;
			char str[128];
			int bytes = snprintf(str, 128, "49=%s\00156=%s\001",
					FixUtilConfig::get_sender_comp(m_config).c_str(),
					FixUtilConfig::get_target_comp(m_config).c_str());
			m_headerStr = (char*)malloc(bytes + 1);
			memcpy(m_headerStr, str, bytes + 1);
			m_headerStrLen = bytes;
		}

		void sendLogin(bool resetSeq=false);
		void sendLogout(const char* text = NULL);
		void sendHB(unsigned long long testReqId = 0);
		void setLoginState();
		void logAndSend(const char* msg, int size);
		const CConfig &m_config;
		Publisher& m_publisher;
		Session& m_session;
		FixMessageBuilder m_writer;
		FixMessageReader<FieldTerminator> m_reader;

	private:
		// connection states
		const char* m_headerStr;
		int m_headerStrLen;
		unsigned int m_timeCount;
		bool m_socketConnected, m_socketConnecting, m_sessionUp;
		unsigned int m_timeCount, m_seqOut, m_seqIn;
		unsigned int m_seqFillTo, m_seqSkipTo;
		bool m_inRecovery;

		// low level receive and sequence number checking
		void decodeMessage(const char* bodyStart, unsigned int bodyLen, const char* type);
		SeqCheckResponse updateSeqWithMsg(unsigned short type);
		void initSeqNumbers();
		bool readSeqFromConfig();
		bool readSeqFromLog();
		bool writeSeqToLog();
		bool shouldResetSeq(int resetHour, int resetDay, time_t timeSeq) const;
	};

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	FixConnectionBase(const CConfig& config, Publisher& publisher, Session& session, int inSeq, int outSeq)
	: Socket(config, superRcvSocket),
	  m_config(config),
	  m_publisher(publisher),
	  m_session(session),
	  m_headerStr(NULL),
	  m_headerStrLen(0),
	  m_timeCount(-1),
	  m_seqOut(inSeq),
	  m_seqIn(outSeq),
	  m_socketConnected(false),
	  m_socketConnecting(false),
	  m_sessionUp(false),
	  m_writer(FixUtilConfig::get_version_string(config).c_str()),
	  m_inRecovery(false),
	  m_seqFillTo(0),
	  m_seqSkipTo(0)
	{
		prepareHeaderStr();
		initSeqNumbers();
		char timeStr[32];
		timeStr[getFixSendingTimeMS(timeStr)]=0;
		m_publisher.logInfo("Fix connection created at %s, info: %s\n",
				timeStr,
				getLineInfo().c_str());
	};

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	~FixConnectionBase()
	{
		// free memory
		if (m_headerStr) {
			free (m_headerStr);
			m_headerStr = NULL;
		}
		static_cast<Derived*>(this)->sendLogout();
		m_sessionUp = false;
		m_socketConnected = false;
		Socket::shutDown();
		m_publisher.logInfo("FixControlBase Destructed for User: %s\n", FixUtilConfig::get_username(config).c_str());
		writeSeqToLog();
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	initSeqNumbers() {
		if ((m_seqIn >= 0) && (m_seqOut >= 0)) return;
		if (!readSeqFromConfig()) {
			if (!readSeqFromLog()) {
				m_seqIn = 0;
				m_seqOut = 0;
				m_publisher.logInfo("FixControlBase Sequence Number reset.\n");
			}
		}
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	bool FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	readSeqFromConfig() {
		if (m_seqIn < 0)
			m_seqIn = FixUtilConfig::get_seq_in(m_config);
		if (m_seqOut < 0)
			m_seqOut = FixUtilConfig::get_seq_out(m_config);

		if ((m_seqIn >= 0) && (m_seqOut >= 0)) {
			m_publisher.logInfo("FixControlBase Sequence Number "
					"initialized to in/out: %u/%u\n", m_seqIn, m_seqOut);
			return true;
		}
		return false;
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	bool FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	readSeqFromLog() {
		char seqFileName[128];
		snprintf(seqFileName, 128, "%s_%d.fixseq",
				m_publisher.getLogFileName().c_str(),
				SocketUtils::SocketConfig::get_server_port(m_config));
		FILE *fp = fopen(seqFileName, "rt");
		bool ok = false;
		if (fp)
		{
			unsigned int seqIn, seqOut, timeSeq;
			if (fscanf(fp, "%u, %u, %u", &seqIn, &seqOut, &timeSeq) == 3)
			{
				int resetHour = static_cast<Derived*>(this)->getSequenceResetHour();
				int resetDay = static_cast<Derived*>(this)->getSequenceResetDay();
				if (shouldResetSeq(resetHour, resetDay, timeSeq))
				{
					// resetting
					if (m_seqIn < 0) m_seqIn = 0;
					if (m_seqOut < 0) m_seqOut = 0;

					time_t currTime;
					time(&currTime);
					m_publisher.logInfo("FixControlBase Sequence Number too old, "
							"resetting. Read %u/%u from %s, resetting\n",
							m_seqIn, m_seqOut, ctime(&currTime));
				} else {
					// restoring
					if (m_seqIn < 0) m_seqIn = seqIn;
					if (m_seqOut < 0) m_seqOut = seqOut;
					m_publisher.logInfo("FixControlBase Sequence Number read "
							"from previous log to in/out: %u/%u from\n",
							m_seqIn, m_seqOut);
					ok = true;
				}
			}
			fclose(fp);
		}
		return ok;
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	bool FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	writeSeqToLog() {
		char seqFileName[128];
		snprintf(seqFileName, 128, "%s_%d.fixseq", m_publisher.getLogFileName().c_str(), SocketUtils::SocketConfig::get_server_port(m_config));
		FILE *fp = fopen(seqFileName, "wt");
		if (fp)
		{
			time_t curTimeSec;
			time(&curTimeSec);
			fprintf(fp, "%u, %u, %u", m_seqIn, m_seqOut, (unsigned int) curTimeSec);
			fclose(fp);
			return true;
		}
		return false;
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	virtual void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	onReceive(const void*buf, int size, bool* bIsDeleted = NULL)
	{
#ifdef DEBUG_FIX_UTILS
	m_publisher.logDebug("Received %d bytes: %s\n", size, dumpFixMsg((const char*)buf, size).c_str());
#endif
		if (size > 0)
		{
			int remainingBytes = size;
			const char* msgStart = (const char*) buf;
			while (m_socketConnected && (remainingBytes > 0)) {
				const char* bodyStart;
				unsigned int bodyLen;
				char msgType[4];
				int msgSize = getMsgSize(msgStart, remainingBytes, bodyStart, bodyLen, msgType);
				if (msgSize > 0)
				{
					decodeMessage(bodyStart, bodyLen, msgType);
					msgStart += msgSize;
					remainingBytes -= msgSize;
				} else {
					if (__builtin_expect((msgSize < 0), 0)) {
						m_publisher.logAlert("received invalid message, closing session: %s\n",
								dumpFixMsg((const char*)msgStart, remainingBytes).c_str());
						m_publisher.onClose();
						return ;
					}
					Socket::storePartialMsg(msgStart, remainingBytes);
					break;
				}
			};
		};
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	virtual void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	onError(int errorCode, const char* errorMsg)
	{
		m_publisher.logAlert("Error: Currenex Ouch Socket Error %d: %s, closing socket\n", errorCode, errorMsg);
		m_publisher.onClose();
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	virtual void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	onClose()
	{
		m_publisher.onClose();
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	virtual void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	onConnect()
	{
		m_socketConnected = true;
		m_socketConnecting = false;
		static_cast<Derived*>(this)->sendLogin();
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	virtual void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	sessionConnect()
	{
		if (!m_socketConnected && !m_socketConnecting)  {
			m_socketConnecting = true;
			Socket::callConnectSocket();
		}
	};

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	onOneSec()
	{
#ifdef DEBUG_FIX_UTILS
		if ((m_timeCount & 7) == 0)
		{
			m_publisher.logDebug("on One Second at FixControlBase, %s\n", getLineInfo().c_str());
		}
#endif
		if (!m_socketConnected) return;
		m_timeCount++;
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	std::string FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	getLineInfo() const {
		char buf[512];
		sprintf(buf, "SenderComp: %s, TargetComp: %s, User: %s, Msg Sent: %u, Received: %u, Been Connected For (hh-mm-ss): %02d-%02d-%02d",
				FixUtilConfig::get_sender_comp(config).c_str(),
				FixUtilConfig::get_target_comp(config).c_str(),
				static_cast<Derived*>(this)->getUserName(),
				m_seqOut,
				m_seqIn,
				m_timeCount/3600,
				(m_timeCount%3600)/60,
				m_timeCount%60);
		return std::string(buf);
	}

	/// the following functions are called by FixSessions for composition

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	template<typename BuilderType>
	BuilderType& FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	makeHeader(BuilderType& builder, const char* type, unsigned int seq) {
		// TODO pre-write senderComp + targetComp
		builder.resetType(type)
				.writeString(m_headerStr, m_headerStrLen)  // sender/target comp id
				.writeIntField(34, seq)  // sequence number
				.writeCurrentTime(52);
		return static_cast<Derived*>(this)->writeAdditionalHeaderField(builder);
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	sendLogin(bool resetSeq=false)
	{
		makeHeader(m_writer, "A", ++m_seqOut).
				writeField(98, '0').
				writeIntField(108, Default_HB_Interval).
				writeField(141, resetSeq?'Y':'N').
				writeField(553, FixUtilConfig::get_username(config).c_str()).
				writeField(554, FixUtilConfig::get_password(config).c_str()).
				finalizeAndSend(this);
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	sendLogout(const char* text = NULL) {
		makeHeader(m_writer, "5", ++m_seqOut);
		if (text) {
			m_writer.writeField(58, text);
		}
		m_writer.finalizeAndSend(this);
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	sendHB(unsigned long long testReqId = 0) {
		makeHeader(m_writer, "0", ++m_seqOut);
		if (testReqId != 0) {
			m_writer.writeIntField(112, testReqId);
		}
		m_writer.finalizeAndSend(this);
	}


	template<typename Derived, typename Publisher, typename Session, typename Socket>
	void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	setLoginState() {
		if (!m_sessionUp) {
			 m_sessionUp = true;
			 m_publisher.onConnect();
			 m_publisher.logInfo("Fix Connection logged in: %s\n", getLineInfo().c_str());
		}
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	logAndSend(const char* msg, int size) {
#ifdef DEBUG_FIX
		m_publisher.logDebug("==> %s\n", dumpFixMsg(msg, size).c_str());
#endif
		Socket::sendToSocket(msg, size);
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	void FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	decodeMessage(const char* bodyStart, unsigned int bodyLen, const char* type) {
		m_reader.bind(bodyStart, bodyLen);
		unsigned short typeShort = GetTypeShort(type);
		switch (updateSeqWithMsg(typeShort)) {
		case SeqCheckOK:
			// good, fall through
			break;
		case SeqCheckDisconnect:
			// closing socket and return;
			m_publisher.onClose();
			return;
		case SeqCheckskip:
			// ignore this message
			return;
		}

		// Data session takes a higher priority
		if (m_session.parse(m_reader, typeShort)) return;

		// Control stuff goes here
		switch (typeShort) {
		case HeartBeatTypeShort:
			sendHB();
			break;
		case TestRequestTypeShort:
			sendHB();
			break;
		case LogoutTypeShort:
			// logout
			static_cast<Derived*>(this)->parseSeqFromLogout();
			m_publisher.logAlert("Logout received from %s, closing connection\n", m_targetComp.c_str());
			m_publisher.onClose();
			break;
		case LogonTypeShort:
			setLoginState();
			break;
		}
	}

	template<typename Derived, typename Publisher, typename Session, typename Socket>
	SeqCheckResponse FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	updateSeqWithMsg(unsigned short type)
	{
		bool ok;
		unsigned int seq = m_reader.getUnsignedInt(34, ok);
		// we probably don't need to check this unless something very wrong happened
		if (__builtin_expect((!ok), 0)) {
			m_publisher.logAlert("got invalid sequence number, closing socket\n");
			return SeqCheckDisconnect;
		}

		// Highest priority: handle logon with sequence reset separately
		if (__builtin_expect((type == LogonTypeShort), 0)) {
			char seqReset = m_reader.getChar(141, ok);
			if (ok && (seqReset == 'Y')) {
				// sequence reset
				m_seqIn = 1;
				m_seqOut = 0;
				sendLogin(true);
				return SeqCheckskip;
			}
		}

		SeqCheckResponse response = SeqCheckOK;
		if (__builtin_expect((type == SequenceResetTypeShort), 0)) {
			// handle sequence reset message before checking sequence number
			// if GapFillFlag (123) is not set or 'N', we should ignore
			// it's seq number, but to blindly reset to the new Seq No (36)
			bool ok;
			unsigned int newSeq = m_reader.getUnsignedInt(36, ok);
			if (newSeq <= m_seqIn + 1) {
				// we received duplicate resend, ignore
				m_publisher.logInfo("got duplicate gap fill, ignore\n");
				return SeqCheckskip;
			}
			m_seqIn = newSeq - 2;
			seq = newSeq -1;  // pretend that we received that seq
			response = SeqCheckskip;
		}

		// normal operation: check sequence number
		if (__builtin_expect((seq == ++m_seqIn), 1)) {
			if (__builtin_expect((m_inRecovery), 0))
			{
				// check if we are out of recovery yet
				// Here, we wait for the new msg for out-of-seq
				if (m_seqIn == m_seqFillTo + 1)
				{
					// we've recovered from a previous gap
					m_inRecovery = false;
					setLoginState();
				}
			}
			return response;
		}

		// we've got a problem with incoming sequence number
		if (seq < m_seqIn) {
			char msgBuf[64];
			snprintf(msgBuf, 64, "MsgSeqNum too low, expecting %u but received %u", m_seqIn, seq);
			sendLogout(msgBuf);
			m_publisher.logAlert("%s, closing socket\n", msgBuf);
			return SeqCheckDisconnect;
		}

		// we've detected a gap
		if (m_inRecovery) {
			// we've got a gap during the recovery,
			// only possibility should be the in-flight packet
			// we will ignore those as they should resend send us
			// all missing packets plus the in-flight ones
			if (seq > m_seqFillTo)
				m_seqFillTo = seq;
			return SeqCheckskip;
		}

		// send a resend request
		makeHeader(m_writer, "2", ++m_seqOut).
			writeField(7, m_seqIn).
			writeIntField(6, seq + 1024).	// set end seq to be much larger
											// to avoid multiple requests
			finalizeAndSend(this);
		--m_seqIn;
		m_inRecovery = true;
		m_seqFillTo = seq;
		return SeqCheckskip;
	}

	// if resetHour is -1, no sequence reset - always return false
	// if resetDay is -1, daily reset on resetHour,
	// otherwise, weekly reset on the day/hour
	// timeSeq is the last log time.
	// It compares timeSeq with curTime, check if they
	// are on the same side of reset point.
	template<typename Derived, typename Publisher, typename Session, typename Socket>
	bool FixConnectionBase<typename Derived, typename Publisher, typename Session, typename Socket>::
	shouldResetSeq(int resetHour, int resetDay, time_t timeSeq) const {
		if (resetHour == -1)
			return false;
		struct tm prevTime, curTime;
		localtime_r(&timeSeq, &prevTime);
		time_t timeNow = time(NULL);
		localtime_r(&timeNow, &curTime);
		int prevHour = 0, curHour = 0, resetPoint = 0, modAmount = 24*3600;
		if (resetDay != -1) {
			prevHour = prevTime.tm_wday * 24 * 3600;
			curHour = curTime.tm_wday * 24 * 3600;
			resetPoint = resetDay * 24 * 3600;
			modAmount += (6*24 * 3600);
		}
		prevHour += (prevTime.tm_hour*3600 + prevTime.tm_min*60 + prevTime.tm_sec);
		curHour += (curTime.tm_hour*3600 + curTime.tm_min*60 + curTime.tm_sec);
		resetPoint += (resetHour*3600);
		if (curHour < prevHour) {
			curHour += modAmount;
		}
		if ((curHour >= resetPoint) && (prevHour <= resetPoint))
			return true;
		return false;
	}
}
