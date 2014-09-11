#include <Vulcan/Common/FixUtils/FixUtils.h>
#include <Misc/utils/Config.h>

namespace FixUtils {
	template<typename Publisher, typename Derived>
	class BaseComposer {
	public:
		BaseComposer(const CConfig &config, Publisher& publisher) :
			m_publisher(publisher),
			m_senderComp(FixUtilConfig::get_sender_comp(config)),
			m_targetComp(FixUtilConfig::get_target_comp(config)),
			m_username(FixUtilConfig::get_username(config)),
			m_password(FixUtilConfig::get_password(config)),
			m_versionString(FixUtilConfig::get_version_string(config))
	{

	}
		~BaseComposer();

	protected:
		Publisher& m_publisher;
		const CString m_senderComp;
		const CString m_targetComp;
		const CString m_username;
		const CString m_password;
		const CString m_versionString;
		int makeHeader(unsigned int seq) const {
			// makes the header portion
			// Header
			// 8=FIX.4.3
			// 9 	len
			// 35 	type
			// 34 	seq
			// (43) 	possDup
			// 49	senderComp
			// (50)	senderSubID
			// 56	targetComp
			// (57)	targetSubID
			// (115)	onBehalfOfCompID
			// (116)
			// (128)	deliverToCompID
			// (129)
			// 52	sendingTime
			// (122)

		}

		const std::string getUserName() const {
			if (m_username.GetLength() == 0)
				return m_senderComp.c_str();
			return m_username.c_str();
		}

		int composeLogon(char* msg, unsigned int seq) {

		}

		int composeLogout(char* msg, unsigned int seq) {

		}

		int composeHB(char* msg, unsigned int seq) {

		}

		int composeSessionRequest(char* msg, unsigned int seq) {

		}



	private:

	};
}
