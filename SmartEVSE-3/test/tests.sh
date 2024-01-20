#!/bin/bash

# This script is meant to automate testing of SmartEVSEv3 firmware
# Needs jq binary

#serialnrs (4 digits):

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 serialnr_of_master serialnr_of_slave ; serialnr is 4 digits."
    exit 1
fi

MASTER="smartevse-"$1".local"
SLAVE="smartevse-"$2".local"

#curl suppress data
CURLPOST="curl -s -o /dev/null -X POST"

# Colors for echo
# Reset
NC='\033[0m'       # Text Reset

# Regular Colors
Black='\033[0;30m'        # Black
Red='\033[0;31m'          # Red
Green='\033[0;32m'        # Green
Yellow='\033[0;33m'       # Yellow
Blue='\033[0;34m'         # Blue
Purple='\033[0;35m'       # Purple
Cyan='\033[0;36m'         # Cyan
White='\033[0;37m'        # White

ABORT=0
#read maxcurrent out of settings:
#curl -s -X GET http://smartevse-$MASTER.local/settings | jq ".settings.current_max"

#set maxcurrent:
#curl -X POST http://192.168.2.181/automated_testing?current_max=10

#MODESWITCH TEST: test if mode changes on master reflects on slave and vice versa
$CURLPOST $MASTER/automated_testing?loadbl=1
$CURLPOST $SLAVE/automated_testing?loadbl=2
for mode_master in 1 2 3; do
    $CURLPOST $MASTER/settings?mode=$mode_master
    sleep 3
    mode_slave=$(curl -s -X GET $SLAVE/settings | jq ".mode_id")
    if [ "x"$mode_slave == "x"$mode_master ]; then
        printf "$Green Passed $NC Master switching to $mode_master, slave follows.\n"
    else
        printf "$Red Failed $NC Master switching to $mode_master, slave is at $mode_slave.\n"
        ABORT=1
    fi
done

if [ $ABORT -ne 0 ]; then
    exit $ABORT
fi

for mode_slave in 1 2 3; do
    $CURLPOST $SLAVE/settings?mode=$mode_slave
    sleep 5
    mode_master=$(curl -s -X GET $MASTER/settings | jq ".mode_id")
    if [ "x"$mode_slave == "x"$mode_master ]; then
        printf "$Green Passed $NC Slave switching to $mode_slave, master follows.\n"
    else
        printf "$Red Failed $NC Slave switching to $mode_slave, master is at $mode_master.\n"
        ABORT=1
    fi
done

#if we cannot rely on correct switching of modes between master and slave, we will have to abort testing
if [ $ABORT -ne 0 ]; then
    exit $ABORT
fi

exit 0

for loadbl_master in 0 1; do
    curl -X POST $MASTER/automated_testing?loadbl=$loadbl_master
    for mode_master in 1 2 3; do
        curl -X POST $MASTER/settings?mode=$mode_master
        sleep 3
        LBL=$(curl -s -X GET $MASTER/settings | jq ".evse.loadbl")
        MODE=$(curl -s -X GET $MASTER/settings | jq ".mode")
        echo LOADBL=$LBL, MODE=$MODE
    done
done


