#!/bin/bash

set -x 

TIMESTAMP=`date "+%Y%m%d"`

DATADIR=data/EURUSD/IB/2013

CONFFILE=model/conf/EURUSD.IB.20140214.conf

PROPFILE=model/conf/EURUSD.IB.20140214.prop
TRAINFILE=$DATADIR/train.dat
MODELFILE=model/EURUSD/model.IB.2013.$TIMESTAMP

for i in $DATADIR/*.csv; do 
    java -cp lib/java/fxstms.jar ClassifierDataManager $CONFFILE $i ${i%.csv}.dat;
done

cat $DATADIR/day.*.dat > $TRAINFILE

TRAINCMD="java -cp lib/java/stanford-classifier-3.3.0.jar:lib/java/fxstms.jar ClassifierTrainer $PROPFILE $TRAINFILE $MODELFILE";
LOGFILE=log/ClassifierTrainer.$TIMESTAMP.log

echo $TRAINCMD > $LOGFILE
echo > $LOGFILE
$TRAINCMD  2>&1 | tee -a $LOGFILE






