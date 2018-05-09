#!/bin/bash
rm *.o
for f in `ls *.cpp` ; do echo $f ; g++ -pthread -Wall -Wno-switch -std=c++11 -g -c $f ; done
ar rvs libib_debug.a *.o
rm *.o
for f in `ls *.cpp` ; do echo $f ; g++ -pthread -Wall -Wno-switch -std=c++11 -O3 -c $f ; done
ar rvs libib.a *.o
rm *.o
# assuming current directory is /cygdrive/e/ib/kisco/src/tp/venue/IB/sdk
# lib is at /cygdrive/e/ib/kisco/lib
mv *.a ../../../../../lib
