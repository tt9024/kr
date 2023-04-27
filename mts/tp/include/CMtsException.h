#ifndef CMtsEXCEPTION_HEADER

#define CMtsEXCEPTION_HEADER

#include <string>
#include <stdexcept>

namespace Mts
{
	namespace Exception
	{
		class CMtsException : public std::exception
		{
		public:
			CMtsException(const std::string & strErrorMsg);

			~CMtsException() throw();

			virtual const char * what() const throw();

		private:
			std::string		m_strErrorMsg;
		};
	}
}

#endif
