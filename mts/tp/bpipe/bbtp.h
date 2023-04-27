#include <blpapi_defs.h>
#include <blpapi_correlationid.h>
#include <blpapi_element.h>
#include <blpapi_event.h>
#include <blpapi_message.h>
#include <blpapi_session.h>
#include <blpapi_subscriptionlist.h>
#include <blpapi_tlsoptions.h>

// for tp publish
#include "md_bar.h"

namespace tp {
namespace bpipe {

struct BPIPE_Config
{
public:
    std::string              d_pub;
    std::vector<std::string> d_hosts;
    int                      d_port;
    std::string              d_service;
    std::vector<std::string> d_topics;
    std::set<std::string>    d_topics_primary;
    std::vector<std::string> d_fields;
    std::vector<std::string> d_options;  // subscription options, not used
    std::string              d_authOptions;  // app and user
    std::string              d_clientCredentials;
    std::string              d_clientCredentialsPassword;
    std::string              d_trustMaterial;

    explicit BPIPE_Config(const char* config_file = NULL);
    BloombergLP::blpapi::TlsOptions getTlsOptions() const;
    BloombergLP::blpapi::SessionOptions getSessionOptions() const;

    std::string toString() const;

private:
    void setFields();
    void setTopics();
};

class BPIPE_Thread
{
public:
    explicit BPIPE_Thread(const char* config_file = NULL);
    void run();
    void kill();

private:
    const BPIPE_Config m_cfg;
    volatile bool m_should_run;
    std::unordered_map<std::string, double> m_sym_pxmul_map;

    void connect(BloombergLP::blpapi::Session& session);
    void process(const std::string& symbol, 
                 const BloombergLP::blpapi::Message& msg, 
                 md::MD_Publisher& pub);
    bool authorize(const BloombergLP::blpapi::Service &authService,
                         BloombergLP::blpapi::Identity *subscriptionIdentity,
                         BloombergLP::blpapi::Session *session);

    bool checkFailure(const BloombergLP::blpapi::Event::EventType& eventType, const BloombergLP::blpapi::Message& message) const;
    const char* getTopic(const BloombergLP::blpapi::Message& msg) const;
    const char* getSymbol(const char* topic) const;
    std::string printMsg(const BloombergLP::blpapi::Message& msg) const;
};

} // namespace bpipe
} // namespace tp
