#!/bin/bash

set -x 


CONFFILE=model/conf/EURUSD.IB.20140214.conf
TIMESTAMP=`date "+%s"`

java -cp lib/java/stanford-classifier-3.3.0.jar:lib/java/fxstms.jar ClassifierServer $CONFFILE | tee log/ClassifierServer.$TIMESTAMP.log 2>&1


