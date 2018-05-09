CXX=g++
CXXFLAGS=-pthread -Wall -Wno-switch -std=c++11 -g
ROOT_DIR=../../../source/cppclient
BASE_SRC_DIR=${ROOT_DIR}/client
INCLUDES=-I${BASE_SRC_DIR} -I${ROOT_DIR}
TARGET=TestCppClient

$(TARGET):
	$(CXX) $(CXXFLAGS) $(INCLUDES) ./*.cpp -o$(TARGET) /cygdrive/e/ib/kisco/lib/iblib.a -I/cygdrive/e/ib/kisco/src/tp/venue/IB/sdk

clean:
	rm -f $(TARGET) *.o

