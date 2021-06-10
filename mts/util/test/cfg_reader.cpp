#include "plcc/PLCC.hpp"
#include <iostream>


int main(int argc, char** argv) {
    utils::ConfigureReader cr(argv[1]);
    std::cout << cr.toString() << std::endl;
    return 0;
}
