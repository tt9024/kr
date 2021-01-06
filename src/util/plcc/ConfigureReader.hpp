#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <stdexcept>
#include <vector>
#include <map>

#include <memory>
#include <iostream>
#include <sstream>
#include <type_traits>

/* ----------------------------------
 * A configuration File Parser/Writer
 * ----------------------------------
 *
 * Config file in the format:
 *
 * key = value \n                     (1)
 * key = { key1 = value1 \n ... } \n  (2)
 * key = [ value, ... , value ] \n    (3)
 *
 * (1): basic form, a new line is needed for each kv pair
 *      * key and value are allowed to have white-space char in the middle,
 *      * key is taken as white-space stripped string between last '\n', or '{' and '='
 *      * value is taken as white-space stripped string between '=' and '\n'
 * (2): composite form with '{' and '}'
 *      * same key/value requirements with (1)
 *      * each key/value pair in each line
 * (3): array defined within '[' and ']', with ',' as delimiter
 *      * values are taken as white-space, new-line, tab,  stripped string between
 *        previous ',' (or '[') and the next ',' (or ']')
 * (4)  comment is allowed after '#' until a new-line.
 * (5): special charactors that are used to parse the config include 
 *      '=', '{', '}', '[', ']', ',', '\n', '#'
 *      escape these charactor use '/'. Characor '/' should be escaped by itself.
 *      leading and trailing white-space can be escaped as well. white-spece in the
 *      middle doesn't need to be escaped.
 *      comment is allowed after '#' until a new-line
 * (6): Charactors in config file is assumed to be printable i.e. > 32. 
 *      In particular, special charactors [16, 23] are not allowed, they are used internally
 *      to represent the special charactors.
 * (7): Currently the configure doesn't support value/array at root level. 
 *      i.e. at root level, a value or an array without a key is illegal.
 *
 * When querying, use the '.' to navigate tree and '[]' for array elements.
 * For example:
 * Query: 'SPX.contracts[1].exch_symbol'
 * Results in looking into composit value of SPX's array of contracts element 1's value of 'exch_symbol'
 *
 * The value can be update and persisted to a same formated config file.
 */

namespace utils {

class ConfigureReader {
public:
    // Value object which could hold one of three types: a map, an array or a simple value
    struct Value;
    using ConfigMapType = std::map<std::string, Value>;
    struct Value {
        // todo - consider move to a anonyous union, but the
        // cost of otherwise is not high, so lower priority. 
        //
        // enum {Compisit, Array, Value} tag
        // union {
        ConfigMapType _kv;
        std::vector<Value> _array;
        std::string _value;
        // }

        // gets simple value or array value from current Value object,
        // and convert to the given data types.  
        // allowed types for simple values are int/long long/double and string
        // and vector of such types. 
        template<typename T> T get() const;
        template<typename T> std::vector<T> getArr() const;

        // gets composite key-value map from current Value object.
        // The key could have '.' or '[]' to suggest the composite or arrays
        // Example: asset.SPX[1].exch_symbol
        const Value* query(const std::string& key) const;

        // persists the current value into a ConfigureReadable format
        std::string toString(int indent = 2, bool array_value = false) const;
        bool operator== (const Value& val) const;

        // set the given "val" as a simple value or an array of simple values to the current Value object.
        // allowed simple value types are int/long long/double and string
        // if val is vector of these types, it makes this Value object to be an array of such type
        template<typename T> void set(const T& val);
        template<typename T> void setArr(const std::vector<T>& val);

        // it append an element to the array in the Value object
        // returns a reference of newly appended Value object to be modified.
        Value* addArrayElement();

        // adds key to the composite map of this Value object
        // if over_write is false, throws if key exists
        Value* addKey(const std::string& key, bool over_write=true);

        // removes all the settings in the Value object
        void clear();
    private:
        // getter (partial) specializations
        //template<> int  get<int>() const;
        //template<> long long get<long long>() const;
        //template<> double get<double>() const;
        //template<> std::string get<std::string>() const;
        template<typename T>
        std::string valToString(const T& v) const;

        struct KeyItem {
            std::string _key;
            int _idx;
            KeyItem(const std::string& k, int ix): _key(k), _idx(ix) {};
        };
        bool getKeyItems(const std::string& key,  std::vector<KeyItem>& ki) const;

        void assertArray() const;
        void assertMap() const;
        void assertValue() const;
    };

public:
    explicit ConfigureReader(const char* configFileName);
    //explicit ConfigureReader(const ConfigureReader& reader);
    void operator=(const ConfigureReader& reader);
    ~ConfigureReader();

    // clear and reload
    std::string reset(const std::string& configFileName = "");

    // retrieve for simple values or array
    // The key could have '.' or '[]' to suggest the composite or arrays
    // this would throw if key not found
    // Example: asset.SPX[1].exch_symbol
    template<typename T> T              get(const char* key) const;
    template<typename T> std::vector<T> getArr(const char* key) const;

    // retrieve simple value or array with default value
    template<typename T> T get(const char* key, bool* found, const T& defaultVal) const;
    template<typename T> std::vector<T> getArr(const char* key, bool* found, const std::vector<T>& defaultVal) const;

    // retrieve a composite key-value map with the key for reading purpose
    ConfigureReader getReader(const std::string& key) const;

    // lists all keys if current Value object is a map
    // otherwise, returns empty vector
    std::vector<std::string> listKeys() const;

    // return size of array if current Value object is an array
    // otherwise, returns zero
    size_t arraySize() const;
    
    // persist the current settings to a string readable to both ConfigureReader and human
    std::string toString() const;

    // Compare two config, for test purpose. The array sequence has to be the same in order to be equal
    bool operator==(const ConfigureReader& reader) const;


    // set a key to be the simple value or an array
    // the key can have '.' or '[]'
    template<typename T> void set   (const std::string& key, const T& val);
    template<typename T> void setArr(const std::string& key, const std::vector<T>& val);

    // create a key, return a value to be manipulated with Value's interface
    // in particular, when key = "", it returns the root, i.e. m_value,
    // without modifying anything yet.
    // This allows iteratively set at the Value object level, using Value's
    // set() interfaces
    Value* set(const std::string& key);

    static const char* get_spchar_map();
    static const char* get_escape_map();

private:
    // special char needs to be escaped
    static const char EQ = 16; // =
    static const char OC = 17; // {
    static const char CC = 18; // }
    static const char OB = 19; // [
    static const char CB = 20; // ]
    static const char CM = 21; // ,
    static const char NL = 22; // \n     needs to be restored in value
    static const char WS = 23; // space  needs to be restored in value

    // A configure is a set of key-value pairs as a map
    std::string m_configFileName;
    Value m_value;

    //const helper functions
    explicit ConfigureReader(const Value& value);
    char* readFile(const char* file_path) const;
    bool isWhiteSpace(const char ch) const;
    char* scan(char* bs, char* be, bool ignore_special_char=false) const;
    std::string toConfigString (const char* ks, const char* ke) const;
    bool isSpace(char c) const;
    void strip(const char*& ks, const char*& ke) const;
    const char* match(const char* s, const char* e, char match_char) const;
    const char* findValue(const char* s, const char* e, const char*&ks, const char*&ke, char end_char = NL) const;
    const char* findKey(const char* s, const char* e, const char*&ks, const char*&ke) const;
    bool parseValue(const char* s, const char* e, Value& val) const;
    bool parseKeyValue(const char* s, const char* e, ConfigMapType& kv) const;
};

/*
 * Template Implementations
 */

// these would throw if conversion fails
template<typename T>
T ConfigureReader::Value::get() const {
    // gets the value of current Value object
    // allowed types for simple values are int/long long/double and string
    // if T is of vector type, it gets an array of simple values with allowed types.
    //
    // this could throw if type conversion fails
    // i.e. if trying to convert a string to a number
    
    // all other types are not supported, yet...
    static_assert ((std::is_same<T, int>::value) ||
                   (std::is_same<T, long long>::value) ||
                   (std::is_same<T, double>::value)  ||
                   (std::is_same<T, std::string>::value));
};

template<> inline int  ConfigureReader::Value::get<int>() const                { return std::stoi(_value); };
template<> inline long long ConfigureReader::Value::get<long long>() const     { return std::stoll(_value); };
template<> inline double ConfigureReader::Value::get<double>() const           { return std::stod(_value); };
template<> inline std::string ConfigureReader::Value::get<std::string>() const { return _value; };

template<typename T>
std::vector<T> ConfigureReader::Value::getArr() const {
    assertArray();
    std::vector<T> vec;
    for (const auto& v : _array) {
        vec.push_back(v.get<T>());
    }
    return vec;
}

template<typename T>
std::string ConfigureReader::Value::valToString(const T& val) const {
    std::ostringstream oss;
    oss << val;
    return oss.str();
}

template<typename T>
void ConfigureReader::Value::set(const T& val) {
    // set the given "val" as a simple value or an array of simple values to the current Value object.
    // allowed simple value types are int/long long/double and string

    // all other types are not supported, yet...
    static_assert ((std::is_same<T, int>::value) ||
                   (std::is_same<T, long long>::value) ||
                   (std::is_same<T, double>::value)  ||
                   (std::is_same<T, std::string>::value));
}

template<> inline void ConfigureReader::Value::set<int>(const int& val) { clear() ; _value = valToString(val); };
template<> inline void ConfigureReader::Value::set<long long>(const long long& val) { clear() ; _value = valToString(val); };
template<> inline void ConfigureReader::Value::set<double>(const double& val) { clear() ; _value = valToString(val); };
template<> inline void ConfigureReader::Value::set<std::string>(const std::string& val) { clear() ; _value = val; };

template<typename T>
void ConfigureReader::Value::setArr(const std::vector<T>& val) {
    clear();
    for (const auto& v:val) {
        addArrayElement()->set(v);
    }
}

// getters and setters of ConfigureReader
template<typename T> T ConfigureReader::get(const char* key) const {
    // this throws if key doesn't exist
    bool found = false;
    T ret = get(key, &found, T());
    if (!found) {
        throw std::runtime_error(key + std::string(" not found!"));
    }
    return ret;
}

template<typename T> T ConfigureReader::get(const char* key, bool* found, const T& defaultVal) const {
    // this doesn't throw, but returns found and use default value
    *found = false;
    try {
        const auto* v = m_value.query(key);
        if (!v) {
            return defaultVal;
        }
        *found = true;
        return v->get<T>();
    } catch (const std::exception& e) {
        *found = false;
        return defaultVal;
    }
}

// getters and setters of ConfigureReader
template<typename T> std::vector<T> ConfigureReader::getArr(const char* key) const {
    // this throws if key doesn't exist
    bool found = false;
    auto ret = getArr(key, &found, std::vector<T>());
    if (!found) {
        throw std::runtime_error(key + std::string(" not found!"));
    }
    return ret;
}

template<typename T> std::vector<T> ConfigureReader::getArr(const char* key, bool* found, const std::vector<T>& defaultVal) const {
    // this doesn't throw, but returns found and use default value
    *found = false;
    try {
        const auto* v = m_value.query(key);
        if (!v) {
            return defaultVal;
        }
        *found = true;
        return v->getArr<T>();
    } catch (const std::exception& e) {
        fprintf(stderr, "got exception getting array: %s\n", e.what());
        *found = false;
        return defaultVal;
    }
}

template<typename T>
void ConfigureReader::set(const std::string& key, const T& val) {
    set(key)->set(val);
}

template<typename T>
void ConfigureReader::setArr(const std::string& key, const std::vector<T>& val) {
    set(key)->setArr(val);
}

/**
 *
 * Implementations of inline and helper funtions
 *
 **/

inline
std::vector<std::string> ConfigureReader::listKeys() const {
    std::vector<std::string> vec;
    for (const auto& kv : m_value._kv) {
        vec.push_back(kv.first);
    }
    return vec;
}

inline
size_t ConfigureReader::arraySize() const {
    return m_value._array.size();
}

inline
ConfigureReader::Value* ConfigureReader::Value::addArrayElement() {
    // it append an element to the array in the Value object
    // returns a reference of newly appended Value object to be modified.
    _kv.clear();
    _value.clear();
    _array.push_back(Value());
    return &(_array[_array.size()-1]);
}

inline
void ConfigureReader::Value::assertArray() const {
    if ( (_array.size() == 0) &&
         ((_kv.size() > 0) || (_value.size() > 0))
       ) {
       throw std::runtime_error("Not an array!");
    };
}

inline
void ConfigureReader::Value::assertMap() const {
    if ( (_kv.size() == 0) &&
         ((_array.size() > 0) || (_value.size() > 0))
       ) {
       throw std::runtime_error("Not a map!");
    };
}

inline
void ConfigureReader::Value::assertValue() const {
    if ( (_value.size() == 0) &&
         ((_array.size() > 0) || (_kv.size() > 0))
       )  {
       throw std::runtime_error("Not a simple value!");
    };
}

inline
void ConfigureReader::Value::clear() {
    _value.clear();
    _array.clear();
    _kv.clear();
}

inline
bool ConfigureReader::isWhiteSpace(const char ch) const {
    return ch==' ' || ch=='\t';
}

inline
bool ConfigureReader::isSpace(char c) const  {
    return (c == ' ') || (c == '\t') || (c == '\n') || (c==NL) ;
}

inline
ConfigureReader::ConfigureReader(const char* configFileName) 
: m_configFileName(configFileName)
{
    std::string response = reset();
    if (response != "") {
        throw std::runtime_error(response);
    }
}

/*
inline
ConfigureReader::ConfigureReader(const ConfigureReader& reader)
: m_configFileName(reader.m_configFileName),
  m_value(reader.m_value)
{
}
*/

inline
ConfigureReader::ConfigureReader(const ConfigureReader::Value& value)
:  m_value(value)
{}

inline
void ConfigureReader::operator= (const ConfigureReader& reader)
{
    m_configFileName = reader.m_configFileName;
    m_value = reader.m_value;
}

inline
ConfigureReader::~ConfigureReader() { };

inline
ConfigureReader ConfigureReader::getReader(const std::string& key) const {
    const Value* v = m_value.query(key);
    if (v) {
        return ConfigureReader(*v);
    }
    fprintf(stderr, "Failed to query %s from %s\n", key.c_str(), toString().c_str());
    throw std::runtime_error("Failed to query " + key);
}

inline
ConfigureReader::Value* ConfigureReader::set(const std::string& key) {
    auto* v = m_value.addKey(key, true);
    if (!v) {
        throw std::runtime_error("key " + key + " failed to be added!");
    }
    return v;
}
}
