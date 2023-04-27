#!/bin/bash
while [ 1 ] ; do 
    date >> launch.log 2>&1
    python/launch.py >> launch.log 2>&1 
    echo "Getting out " >> launch.log 2>&1
    date >> launch.log 2>&1
    echo "sleep for 1" >> launch.log 2>&1
    sleep 1
done
