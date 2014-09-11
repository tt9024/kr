#!/bin/bash

set -x 

TIMESTAMP=`date "+%Y%m%d"`

DATADIR2013=data/EURUSD/IB/2013
DATADIR2014=data/EURUSD/IB/2014

CONFFILE=model/conf/EURUSD.IB.20140214.conf

PROPFILE=model/conf/EURUSD.IB.20140214.prop
TRAINFILE=$DATADIR2014/train.dat
MODELFILE=model/EURUSD/model.IB.2013.201401.$TIMESTAMP

#for i in $DATADIR2013/*.csv; do 
#    java -cp lib/java/fxstms.jar ClassifierDataManager $CONFFILE $i ${i%.csv}.dat;
#done

for i in $DATADIR2014/*.30.csv; do 
    java -cp lib/java/fxstms.jar ClassifierDataManager $CONFFILE $i ${i%.csv}.dat;
done

cat $DATADIR2013/day.*.dat > $TRAINFILE

###leave out day 279 for test
for(( j=260; j<279; j++ )); do cat $DATADIR2014/day.$j.30.dat; done >> $TRAINFILE

TRAINCMD="java -cp lib/java/stanford-classifier-3.3.0.jar:lib/java/fxstms.jar ClassifierTrainer $PROPFILE $TRAINFILE $MODELFILE";
LOGFILE=log/ClassifierTrainer.$TIMESTAMP.log

echo $TRAINCMD > $LOGFILE
echo >> $LOGFILE
$TRAINCMD  2>&1 | tee -a $LOGFILE






