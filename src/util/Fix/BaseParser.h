#include <Vulcan/Common/FixUtils/FixUtils.h>
#include <Misc/utils/Config.h>

namespace FixUtils {
	template<typename Publisher, typename Derived>
	class BaseQuoteParser {
	public:
		BaseQuoteParser(const CConfig &config, Publisher& pub);
		~BaseQuoteParser();

		/*** need to implement the following interface ***/

		// return false for not processed message, so the
		// base class will try to process
		// this will parse the message and call publisher for
		// specific level 2
		template<typename FixReader>
		bool parse(FixReader& reader, unsigned short type);

	protected:
		Publisher& publisher;
	};
}
