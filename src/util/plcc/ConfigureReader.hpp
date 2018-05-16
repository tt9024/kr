#include <stdio.h>

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <stdexcept>
#include <map>

namespace utils {

class ConfigureReader {
public:
    typedef std::map<std::string, std::string> ConfigMapType;

    ConfigureReader(const char* configFileName) :
        m_configFileName(configFileName),
        m_configFile(NULL)
    {
        std::string response = reset();
        if (response != "") {
            throw std::runtime_error(response);
        }
    }

    std::string reset() {
        // re-read all the key-values in the pair
        if (m_configFile) {
            fclose(m_configFile);
        }
        if (!(m_configFile = fopen(m_configFileName.c_str(), "rt"))) {
            return (std::string("config file open error"));
        }
        ConfigMapType configMap_;

        // read all lines in
        char strbuf[2048];
        while (fgets(strbuf, sizeof(strbuf), m_configFile)) {
            skipWhiteSpace(strbuf);
            if (strbuf[0] == 0) {
                continue;
            }
            char key[2048], value[2048];
            int n;
            // get the key
            if (sscanf(strbuf, "%2048[^=]%n", key, &n) != 1){
                //return std::string("config file format error");
                //fprintf(stderr, "%s not processed\n", strbuf);
                continue;
            }
            if (strbuf[n] != '=') {
                //fprintf(stderr, "%s not processed\n", strbuf);
                continue;
            }
            if ((sscanf(strbuf+n+1, "%s%n", value, &n) != 1) || (strlen(value) < 1))
            {
                configMap_.erase(key);
                fprintf(stderr, "erase key %s\n", key);
                continue;
            }

            ConfigMapType::iterator iter = configMap_.find(std::string(key));
            if (iter != configMap_.end()) {
                return std::string("duplicate config key");
            }
            configMap_[std::string(key)] = std::string(value);
        }
        m_configMap.clear();
        m_configMap = configMap_;

        return "";
    }
    ~ConfigureReader() {
        if (m_configFile) {
            fclose(m_configFile);
            m_configFile = NULL;
        }
    }

    int getInt(const char* key, bool* found = NULL, int defaultVal = 0) const {
        std::string valStr;
        bool res;
        valStr = findKey(key, &res);
        if (found != NULL) *found = res;
        if (!res) {
            return defaultVal;
        }
        return ::atoi(valStr.c_str());
    }

    std::string getString(const char* key, bool* found = NULL, const std::string defaultVal = "") const {
        std::string valStr;
        bool res;
        valStr = findKey(key, &res);
        if (found != NULL) *found = res;
        if (!res) {
            return defaultVal;
        }
        return valStr;
    }

    double getDouble(const char* key, bool* found = NULL, double defaultVal = 0.0) const {
        std::string valStr;
        bool res;
        valStr = findKey(key, &res);
        if (found != NULL) *found = res;
        if (!res) {
            return defaultVal;
        }
        return ::atof(valStr.c_str());
    }

    std::vector<std::string> getStringArr(const char* key, bool* found = NULL) const {
		std::vector<std::string> ret;
		bool bf;
    	std::string valStr = getString(key, &bf);
    	if (!bf) {
    		if (found) *found=false;
    		return ret;
    	}
    	size_t n = valStr.size();
    	const char* vs=valStr.c_str();
    	if (vs[0]!='[' || vs[n-1]!=']') {
    		if (found) *found=false;
    		return ret;
    	}
    	size_t i0=1;
    	for (size_t i=i0;i<n-1;++i) {
    		if (vs[i]==',') {
    			if (i>i0)
    			    ret.push_back(valStr.substr(i0,i-i0));
    			i0=i+1;
    		}
    	}
    	if (n-1>i0)
    	    ret.push_back(valStr.substr(i0,n-1-i0));

    	if (found) *found=true;
    	return ret;
    }

private:
    std::string m_configFileName;
    FILE* m_configFile;
    ConfigMapType m_configMap;


/*    std::string getConfigString(std::string key) {
        return m_configFile[key];
    }
*/

    void skipWhiteSpace(char* str) {
        char* wptr = str;
        while (*str) {
            if ((*str == ' ') || (*str == '\t')) {
                ++str;
                continue;
            }
            if (*str == '#') {
                break;
            }
            *wptr++ = *str++;
        }
        *wptr = 0;
    }

    std::string findKey(const std::string& key, bool* found) const {
        const ConfigMapType::const_iterator iter = m_configMap.find(key);
        if (iter == m_configMap.end()) {
            if (found) *found = false;
            return "";
        }
        if (found) *found = true;
        return iter->second;
    }

};

}
