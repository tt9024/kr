CXX=g++
#CXXFLAGS=-std=c++0x -DIB_USE_STD_STRING -Wall -Wno-switch -g
CXXFLAGS=-DIB_USE_STD_STRING -Wall -Wno-switch -O3
ROOT_DIR=/cygdrive/e/ib/kisco
BASE_SRC_DIR=${ROOT_DIR}/src
LIBS=-lpthread -lrt -lc lib/libib.a
#LIBS=-lpthread -lrt -lc
OBJ_DIR=${ROOT_DIR}/obj
BIN_DIR=${ROOT_DIR}/bin

INCLUDES=-I${BASE_SRC_DIR}/tp -I${BASE_SRC_DIR}/util -I${BASE_SRC_DIR}/model -I. 

# IB
IB_SRC_DIR=${BASE_SRC_DIR}/tp/venue/IB
IB_INCLUDE=-I${IB_SRC_DIR} -I${IB_SRC_DIR}/sdk -I${IB_SRC_DIR}/sdk/shared

ibclient:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(IB_INCLUDE) -o $(OBJ_DIR)/EClientSocketBase.o -c $(IB_SRC_DIR)/sdk/EClientSocketBase.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(IB_INCLUDE) -o $(OBJ_DIR)/EPosixClientSocket.o -c $(IB_SRC_DIR)/sdk/EPosixClientSocket.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(IB_INCLUDE) -o $(OBJ_DIR)/PosixTestClient.o -c $(IB_SRC_DIR)/PosixTestClient.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(IB_INCLUDE) -o $(OBJ_DIR)/ibclient_test.o -c $(IB_SRC_DIR)/test/ibclient_test.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(BIN_DIR)/$@ $(OBJ_DIR)/EClientSocketBase.o $(OBJ_DIR)/EPosixClientSocket.o $(OBJ_DIR)/PosixTestClient.o $(OBJ_DIR)/ibclient_test.o $(LIBS)

mtrader:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(BIN_DIR)/$@ $(BASE_SRC_DIR)/tp/manual_trader.cpp $(LIBS)

modeltrader:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(BIN_DIR)/$@ $(BASE_SRC_DIR)/model/modelWrap.cpp $(LIBS)

booktap:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(BIN_DIR)/$@ $(BASE_SRC_DIR)/tp/book_reader.cpp $(LIBS)

tickrec:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(BIN_DIR)/$@ $(BASE_SRC_DIR)/tp/tick_recorder.cpp $(LIBS)

### new stuffs
histclient:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(IB_INCLUDE) -o $(OBJ_DIR)/IBClientBase.o -c $(IB_SRC_DIR)/IBClientBase.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(IB_INCLUDE) -o $(OBJ_DIR)/HistClient.o   -c $(IB_SRC_DIR)/HistClient.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(BIN_DIR)/$@ $(OBJ_DIR)/IBClientBase.o $(OBJ_DIR)/HistClient.o $(LIBS)

tpib:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(IB_INCLUDE) -o $(OBJ_DIR)/IBClientBase.o -c $(IB_SRC_DIR)/IBClientBase.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(IB_INCLUDE) -o $(OBJ_DIR)/tpib.o   -c $(IB_SRC_DIR)/tpib.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(BIN_DIR)/$@ $(OBJ_DIR)/IBClientBase.o $(OBJ_DIR)/tpib.o $(LIBS)

clean:
	rm -f $(OBJ_DIR)/*.o
