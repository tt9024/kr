#include "ConfigureReader.hpp"
#include <string.h>

namespace utils {

std::string ConfigureReader::Value::toString(int indent, bool array_value) const {
    // take care of the indent using value()
    if (_kv.size()>0) {
        // iterate composite with indentation
        std::string ret = (array_value?"":("\\\n" + std::string(indent, ' '))) + "{\n";
        for (const auto& kv : _kv) {
            ret += std::string(indent + 2, ' ');
            ret += kv.first;
            ret += " = ";
            ret += kv.second.toString(indent + 4, false);
            ret += "\n";
        }
        ret += (std::string(indent, ' ') + "}");
        return ret;
    } else if (_array.size()>0) {
        std::string ret = (array_value?"":("\\\n" + std::string(indent, ' '))) + "[\n";
        ret += std::string(indent+2, ' ');
        ret += _array[0].toString(indent + 2, true);

        // iterate array values
        for (size_t i = 1; i<_array.size(); ++i) {
            ret += ",\n";
            ret += std::string(indent+2, ' ');
            ret += _array[i].toString(indent + 2, true);
        }
        ret += ("\n" + (std::string(indent, ' ') + "]"));
        return ret;
    }

    // a simple value
    if (_value.size()>0) {
        // restore the escape'ed char, i.e. 
        // change '=' to '\=', etc, and 
        // change NL/WS back to "\ " and "\\n"
        std::string ret = _value;

        // find leading white spaces and trailing white spaces
        // for adding escape to (inner spaces don't need to escape)
        size_t lws = 0;
        for (;lws<ret.size(); ++lws) {
            if (ret[lws] != ' ') break;
        };
        size_t tws = ret.size() - 1;
        for (;tws >= 0; --tws) {
            if (ret[tws] != ' ') break;
        }

        for (size_t i = 0; i<ret.size(); ++i) {
            if (ConfigureReader::get_escape_map()[(int)(ret[i])] != 0) {
                if ((ret[i] != ' ') || (i < lws) || (i > tws)) {
                    ret.insert(i++, "\\");
                }
            }
        }
        return ret;
    }
    return "";
}

bool ConfigureReader::Value::getKeyItems(const std::string& key, std::vector<ConfigureReader::Value::KeyItem>& ki) const {
    if (key.size()==0) {
        return true;
    }
    switch (key[0]) {
    case '.' : 
        return getKeyItems(key.substr(1), ki);
    case '[' :
    {
        // get the index
        size_t cbp;
        int idx;
        try {
            idx = std::stoi(key.substr(1), &cbp);
        } catch (const std::invalid_argument& e) {
            fprintf(stderr, "close bracket not found in the query: %s\n", key.c_str());
            return false;
        }
        std::string next_key = key.substr(cbp+2);
        ki.emplace_back("", idx);
        return getKeyItems(next_key, ki);
    }
    default :
    {
        // if . or [ found, get down the tree
        auto dotp = key.find('.');
        auto sbp = key.find('[');
        std::string k, next_key;
        if ((dotp != std::string::npos) || (sbp != std::string::npos)) {
            if ( ( dotp == std::string::npos ) || 
                 ( (sbp != std::string::npos) && (sbp < dotp) )
               ) 
            {
                // go with '[' 
                k = key.substr(0, sbp);
                next_key = key.substr(sbp);
            } else {
                // go with '.' 
                k = key.substr(0, dotp);
                next_key = key.substr(dotp);
            }
        } else {
            // use the entire string as key
            k = key;
            next_key = "";
        }
        ki.emplace_back(k, 0);
        return getKeyItems(next_key, ki);
    }
    }
}

const ConfigureReader::Value*  ConfigureReader::Value::query(const std::string& key) const {
    std::vector<KeyItem> ki;
    if (!getKeyItems(key, ki)) {
        return NULL;
    }

    const auto* v = this;
    for (const auto& it: ki) {
        const auto& k(it._key);
        const auto& i(it._idx);
        if (k.size()>0) {
            // look for key 
            const auto iter = v->_kv.find(k);
            if (iter == v->_kv.end()) {
                fprintf(stderr, "key %s (in %s) not found in %s\n", k.c_str(), key.c_str(), toString(0, true).c_str());
                return NULL;
            }
            v = &(iter->second);
        } else {
            // look for array
            if (i >=(int) v->_array.size()) {
                fprintf(stderr, "idx %d (in %s) exceeds the size of %d\n", i, key.c_str(), (int)v->_array.size());
                return NULL;
            }
            v = &(v->_array[i]);
        }
    }
    return v;
}

bool ConfigureReader::Value::operator==(const ConfigureReader::Value& val) const {
    if (_value.size() > 0) return _value == val._value;
    if (_array.size() > 0) return _array == val._array;
    if (_kv.size() > 0) 
    {
        for (const auto& kv: _kv) {
            const auto& k (kv.first);
            const auto& v (kv.second);
            const auto iter = val._kv.find(k);
            if (iter == val._kv.end()) {
                return false;
            }
            if ( ! (v == iter->second) ) {
                return false;
            }
        }
        // the other way around
        for (const auto& kv: val._kv) {
            const auto& k (kv.first);
            const auto& v (kv.second);
            const auto iter = _kv.find(k);
            if (iter == _kv.end()) {
                return false;
            }
            if ( ! (v == iter->second) ) {
                return false;
            }
        }
        return true;
    }
    return (val._value.size() == 0) && 
           (val._array.size() == 0) &&
           (val._kv.size() == 0);
}

// modifiers
ConfigureReader::Value* ConfigureReader::Value::addKey(const std::string& key, bool over_write){
    // recursively add key to this Value object
    // key could be specified in similar way with query, except the keys and indexes
    // are added.  
    // if over_write is false, it throws if a key being added exists. 
    // 
    // return a pointer to Value object associated with the key
    // to be modified.
    // NULL if the key cannot be parsed, i.e. '[]' not matching

    std::vector<KeyItem> ki;
    if (!getKeyItems(key, ki)) {
        return NULL;
    }

    auto* v = this;
    for (const auto& it:ki) {
        const auto& k(it._key);
        const auto& i(it._idx);
        if (k.size()>0) {
            // adding a new key k
            auto iter = v->_kv.find(k);
            if ((iter != v->_kv.end()) && (!over_write)) {
                fprintf(stderr, "not overwrite key %s (in %s)! It exists in %s\n", k.c_str(), key.c_str(), toString(0, true).c_str());
                return NULL;
            }
            v->_array.clear();
            v->_value.clear();
            v = &(v->_kv[k]);
        } else {
            // adding an array value
            while (i >= (int) v->_array.size()) {
                v->addArrayElement();
            }
            v = &(v->_array[i]);
        }
    }
    return v;
}

const char* ConfigureReader::get_spchar_map() {
    static char _spchar_map[256];
    static bool _inited = false;

    if (!_inited) {
        memset(_spchar_map, 0, sizeof(_spchar_map));
        for (int i = 0; i<256; ++i) {
            _spchar_map[i] = (char)i;
        };
        _spchar_map[(int)'='] = EQ;
        _spchar_map[(int)'{'] = OC;
        _spchar_map[(int)'}'] = CC;
        _spchar_map[(int)'['] = OB;
        _spchar_map[(int)']'] = CB;
        _spchar_map[(int)','] = CM;
        _spchar_map[(int)'\n']= NL;
        _inited = true;
    }
    return _spchar_map;
}

const char* ConfigureReader::get_escape_map() {
    static char _escape_map[256];
    static bool _inited = false;
    if (!_inited) {
        memset(_escape_map, 0, sizeof(_escape_map));
        _escape_map[(int)'='] = '=';
        _escape_map[(int)'{'] = '{';
        _escape_map[(int)'}'] = '}';
        _escape_map[(int)'['] = '[';
        _escape_map[(int)']'] = ']';
        _escape_map[(int)','] = ',';
        _escape_map[(int)'#'] = '#';
        _escape_map[(int)'\n']= '\n';
        _escape_map[(int)' '] = WS;
        _escape_map[(int)'\\'] = '\\';
        _inited = true;
    }
    return _escape_map;
}

char* ConfigureReader::readFile(const char* file_path) const {
    // read the file into a string.
    FILE* fp = fopen(file_path, "r");
    if (!fp) {
        fprintf(stderr, "ConfigureReader failed to open %s\n", file_path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    size_t sz = ftell(fp);
    if (sz < 3) {
        fprintf(stderr, "ConfigureReader file too small %s\n", file_path);
        fclose(fp);
        return NULL;
    }

    char* buf = (char*) malloc(sz+2);
    fseek(fp, 0, SEEK_SET);
    fread(buf, 1, sz, fp);
    buf[sz] = '\n';
    buf[sz+1] = 0;
    fclose(fp);
    return buf;
}

char* ConfigureReader::scan(char* bs, char* be, bool ignore_special_char) const {
    // 1. remove comments
    // 2. remove lines with only white spaces
    // 3. replace speical charactors
    // 4. remove escape slash for special charactors
    //
    // input: 
    //    bs: the first charactor to be scanned
    //    be: the last charactor to be scanned (not including terminating 0)
    // return: 
    //    pe - the last charactor of modified string.  The modification is in-place, 
    //    therefore [bs, pe] is the string to be used later.
    //    Note no terminating 0 is added, pe is the last charactor, not the terminating 0.
    //
    // state: 0: new line
    //        1: comments
    //        2: escape
    //        3: normal
    int state = 0;
    char* const ps = bs; // ps points to the start of the string, to be modified in *p
    char* p = bs;
    while (bs <= be) {
        if (((int)*bs < 32) && (*bs != '\n') && (*bs != '\t')) {
            if (!ignore_special_char) {
                fprintf(stderr, "unrecognized char with ascii code %d\n", (int) *bs);
                return NULL;
            }
            ++bs;
            continue;
        }

        if (state == 1) {
            // comments, remove until a new line
            if (*bs == '\n') {
                state = 3;
                --bs; // need this new line
            }
            ++bs;
            continue;
        }
        if (state == 2) {
            // escape, have to be a special char or '\'
            if ( get_escape_map()[(int)(*bs)]!= 0) {
                *p++ = get_escape_map()[(int)(*bs++)];
                state = 3;
                continue;
            }
            fprintf(stderr, "error found for escape (forgot to escape \'\\\'?) %s\n", std::string(ps, bs-ps+1).c_str());
            return NULL;
        }
        if (state == 0) {
            // remove white space
            if (isWhiteSpace(*bs) || (*bs == '\n')) {
                // skip leading while spaces or empty lines
                ++bs;
                continue;
            }
            state = 3;
            // fall through
        }

        // state is 3, normal

        switch (*bs) {
        case '#' :
        {
            state = 1;
            break;
        }
        case '\\':
        {
            state = 2;
            break;
        }
        case '\n':
        {
            state = 0;
        }
        default :
            *p++ = get_spchar_map()[(int)*bs];
        }
        ++bs;
        continue;
    }
    return p-1;
}

std::string ConfigureReader::toConfigString (const char* ks, const char* ke) const {
    // convert a scanned string from ks to ke into a 
    // readable string.  
    //
    // It currently simply copy [ks, ke] to string, replacing special chars 
    // WS with ' ', EQ with '=',  CM with ',', NL with '\n'
    std::string ret;
    while (ks<=ke) {
        char c = *ks;
        if (*ks == WS) {
            c = ' ';
        } else if (*ks == EQ) {
            c = '=';
        } else if (*ks == CM) {
            c = ',';
        } else if (*ks == NL) {
            c = '\n';
        } else if (((int)*ks < 32) && (*ks!='\n') &&(*ks!='\t')) {
            // we shouldn't see any special char 
            fprintf(stderr, "special char (%d) detected in key/value of %s\n", (int)*ks, std::string(ks, ke-ks+1).c_str());
            throw std::runtime_error("special char detected in key/value " + std::string(ks, ke-ks+1));
        }
        ret.push_back(c);
        ++ks;
    }
    return ret;
}

void ConfigureReader::strip(const char*& ks, const char*& ke) const {
    // skip space, tab and newlines
    // Note : leading or trailing spaces cannot be escaped
    // should we support it (?)
    while (ks <= ke) {
        if (isSpace(*ks)) 
            ++ks;
        else 
            break;
    }
    while (ks <= ke) {
        if (isSpace(*ke)) 
            --ke;
        else 
            break;
    }
}

const char* ConfigureReader::match(const char* s, const char* e, char match_char) const {
    // matches the next 'match_char' with open/close state matched in the middle
    // return the position of the matching charactor, or NULL if not found
    // matching '{' and '}', '[' and ']' in the middle.  
    // OC/CC , OB/CB are the special charactors replaced from scan, they are replaced
    // because we want to use those charactors in the key/value, with escape'\'
    //
    // A slight complication is the escape of white space.  When escaped, 
    // they need not be stripped as leading/trailing spaces, and therefore the escaped
    // white space has a special code.
    const char* p = s;
    while (p <= e) {
        if (*p == match_char) {
            return p;
        };
        if (*p == OC) {
            p = match(p+1, e, CC);
        } else if (*p == OB) {
            p = match(p+1, e, CB);
        } else if ((*p == CB) || (*p == CC)) {
            fprintf(stderr, "Closing (squre/curly) bracket unexpected at %s\n", std::string(s, p-s+1).c_str());
            return NULL;
        }
        if (!p) {
            return NULL;
        }
        ++p;
    }
    // failed to match
    // return e+1 if it's CM ',', for not final element, or
    // for NL, a new value could be defined without a new line, such as the last value in file
    if ((match_char == CM) || (match_char == NL)) {
        return e+1;
    }
    fprintf(stderr, "failed to match %c given the string: %s\n", match_char, std::string(s, e-s+1).c_str());
    return NULL;
}

const char* ConfigureReader::findValue(const char* s, const char* e, const char*&ks, const char*&ke, char end_char) const {
    // match the end_char, strip the value, return the position after the end_char
    ks = s; 
    ke = match(s, e, end_char);
    if (!ke) {
        return NULL;
    }
    const char* ns = ke+1;
    --ke;
    strip(ks, ke);
    return ns;
}

const char* ConfigureReader::findKey(const char* s, const char* e, const char*&ks, const char*&ke) const {
    const auto* p = findValue(s, e, ks, ke, EQ);
    if (!p) return NULL;

    // check ks and ke doesn't have NL 
    const char* kp = ks;
    while (kp <= ke) {
        if ((*kp == NL) || (*kp == '\n')) {
            fprintf(stderr, "ill-formed key %s from %s\n", std::string(ks, ke-ks+1).c_str(), std::string(s, e-s+1).c_str());
            return NULL;
        }
        ++kp;
    }
    return p;
}

bool ConfigureReader::parseValue(const char* s, const char* e, ConfigureReader::Value& val) const {
    // string between [s, e] already been stripped with white spaces. it represents a simple value or, 
    // if *s starts with '{', parse composition in val as _kv
    // if *s starts with '[', recursively parse each value delimitered by ',' into val as _array
    // empty string is entertained
    if (s>e) {
        val.clear();
        return true;
    }

    switch (*s) {
    case OC :  // '{'
    {
        // parse a composite value, writes to _kv
        return parseKeyValue(s+1, e-1, val._kv);
    }
    case OB :  // '['
    {
        // parse an array of values, writes to _array
        s+=1;
        auto & arr (val._array);
        while (s<=e) {
            const char *ks, *ke;
            s = findValue(s, e-1, ks, ke, CM); // will match last ']' as CM, s points next
            if (!s) {
                // failed to match, just return NULL
                return false;
            }
            arr.emplace_back();
            if (!parseValue(ks, ke, *arr.rbegin())) {
                return false;
            }
        }
        return true;
    }
    default :
    {
        val.clear();
        val._value = toConfigString(s, e);
        return true;
    }
    }
}

bool ConfigureReader::parseKeyValue(const char* s, const char* e, ConfigMapType& kv) const {
    // iteratively gets key, and value 
    if (s>=e) {
        kv.clear();
        return true;
    };
    const char* ks, *ke;
    while (s < e) {
        s = findKey(s, e, ks, ke);
        if (!s) {
            fprintf(stderr,"failed to find the key from %s\n", std::string(s, e-s+1).c_str());
            return false;
        }
        std::string k = toConfigString(ks,ke);
        if (k.size() == 0) {
            fprintf(stderr, "empty key is found before %s\n", std::string(s, e-s+1).c_str());
            return false;
        };
        Value v;
        const char *ks, *ke;
        s = findValue(s, e, ks, ke, NL);
        if (!s) {
            return false;
        }
        if (!parseValue(ks, ke, v)) {
            return false;
        }
        kv[k]=v;
    }
    return true;
}

std::string ConfigureReader::toString() const {
    std::string ret;
    for (const auto& kv : m_value._kv) {
        ret += kv.first;
        ret += " = ";
        ret += kv.second.toString(2, false);
        ret += "\n";
    }
    return ret;
}

bool ConfigureReader::operator==(const ConfigureReader& reader) const {
    return m_value == reader.m_value;
}

// read the configuration file
std::string ConfigureReader::reset(const std::string& configFileName) {
    if (configFileName.size() == 0) {
        if (m_configFileName.size() == 0) {
            fprintf(stderr, "Reload Failed: no config file name specified.  Reloading a temporary value?\n");
            throw std::runtime_error("Reload Failed: no config file name specified.  Reloading a temporary value?");
        };
    } else {
        m_configFileName = configFileName;
    }
    std::string ret = "";
    try {
        char *buf = readFile(m_configFileName.c_str());
        if (!buf) {
            return "config file read error: " + m_configFileName;
        }
        size_t bsz = strlen(buf);
        char* pe = scan(buf, buf+bsz-1);
        if (pe && parseKeyValue(buf, pe, m_value._kv)) {
            fprintf(stderr, "Read config file %s\n", m_configFileName.c_str());
        } else {
            ret = "Failed to read config file " + m_configFileName;
        }
        free (buf);
    } catch (const std::exception& e) {
        ret = std::string("Exception Received: ") + e.what();
    }
    return ret;
}

}
