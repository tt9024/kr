#!/usr/bin/bash

LaunchLog=/home/mts/run/log/launch.log
if [ $1 ] ;
then
    LaunchLog=$1
fi

while [ 1 ] ;
do
    echo "Starting the launch at "`date` >> $LaunchLog
    python/launch.py >> $LaunchLog 2>&1
    echo "Exit the launch at "`date` >> $LaunchLog
    sleep 5
done
