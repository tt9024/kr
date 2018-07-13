/*
 * tpib.cpp
 *
 *  Created on: May 13, 2018
 *      Author: zfu
 */

#include <sstream>
#include <string>
#include <iostream>
#include <cctype>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "tpib.hpp"

tp::TPIB* ibclient;
volatile bool user_stopped = false;

void sig_handler(int signo)
{
  if (signo == SIGINT ||
      signo == SIGTERM ||
	  signo == SIGKILL) {
    logInfo("IBClient received SIGINT, exiting...");
    printf("IBClient received SIGINT, exiting...\n");
  }
  user_stopped = true;
  ibclient->stop();
}

int main() {
    if ((signal(SIGINT, sig_handler) == SIG_ERR) ||
    	(signal(SIGTERM,sig_handler) == SIG_ERR))
    {
            printf("\ncan't catch SIGINT\n");
            return -1;
    }
    utils::PLCC::instance("tpib");
    ibclient=new tp::TPIB();
    ibclient->run();
    delete ibclient;
    return 0;
}
