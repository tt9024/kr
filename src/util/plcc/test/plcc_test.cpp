#include "PLCC.hpp"

#define ConfigFile "config.txt"

using namespace utils;

int main() {
    PLCC plcc(ConfigFile);
    bool f1, f2, f3, f4;
    int intVal=plcc.getInt("IntVal", &f1);

    std::string strVal=plcc.getString("StrVal", &f2, std::string("default"));
    double d1 = plcc.getDouble("NonExistDoulbe", &f3);
    double d2 = plcc.getDouble("NonExistDoulbe", &f3, 3.2);

    printf("IntVal: %d, %d\n StrVal: %s, %d\nNonExistDoulbe: %f,%d\n NonExistDoulbe w/ default: %f,%d\n",
            intVal, (int)f1,
            strVal.c_str(), (int) f2,
            d1, (int)f3,
            d2, (int)f4);
    plcc.logInfo("IntVal: %d, %s", intVal, f1?"Found":"Not Found");
    return 0;
}
