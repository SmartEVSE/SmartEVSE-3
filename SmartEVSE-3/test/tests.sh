#!/bin/bash

# This script is meant to automate testing of SmartEVSEv3 firmware
# Needs jq binary

#serialnrs (4 digits):

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 serialnr_of_master serialnr_of_slave ; serialnr is 4 digits."
    exit 1
fi

# please give values in deci-Ampère:
MASTER_SOCKET_HARDWIRED=320
SLAVE_SOCKET_HARDWIRED=130


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


#SOCKET HARDWIRING TEST: test if Socket resistors in test bench limit our current // Configuration (0:Socket / 1:Fixed Cable)
#                        needs setting [MASTER|SLAVE]_SOCKET_HARDWIRED to the correct values of your test bench

#first load all settings before the test
for device in $MASTER $SLAVE; do
    $CURLPOST $device/automated_testing?config=0
    $CURLPOST $device/automated_testing?config=0
    #save MaxCurrent setting and set it very high
    MAXCUR=$(curl -s -X GET $device/settings | jq ".settings.current_max")
    $CURLPOST $device/automated_testing?current_max=60
    #save MaxCircuit setting and set it very high
    MAXCIRCUIT=$(curl -s -X GET $device/settings | jq ".settings.current_max_circuit")
    $CURLPOST $device/automated_testing?current_max_circuit=70
    #save MaxMains setting and set it very high
    MAXMAINS=$(curl -s -X GET $device/settings | jq ".settings.current_main")
    $CURLPOST $device/automated_testing?current_main=80
done

read -p "Make sure all EVSE's are set to NOT charging, then CHARGING, then press <ENTER>" dummy

for loadbl_master in 0 1; do
    $CURLPOST $MASTER/automated_testing?loadbl=$loadbl_master
    if [ $loadbl_master -eq 1 ]; then
        loadbl_slave=2
    else
        loadbl_slave=0
    fi
    $CURLPOST $SLAVE/automated_testing?loadbl=$loadbl_slave
    #if we are in loadbl 0 we test the slave device in loadbl 0 also
    for mode_master in 1 2 3; do
        $CURLPOST $MASTER/settings?mode=$mode_master
        if [ $loadbl_slave -eq 0 ]; then
            $CURLPOST $SLAVE/settings?mode=$mode_master
        fi
        #settle switching modes AND stabilizing charging speeds
        #LBL=$(curl -s -X GET $MASTER/settings | jq ".evse.loadbl")
        MODE=$(curl -s -X GET $MASTER/settings | jq ".mode")
        #echo LOADBL=$LBL, MODE=$MODE
        printf "Testing with LBL=$loadbl_master, mode=$MODE.\r"
        sleep 10
        CHARGECUR_M=$(curl -s -X GET $MASTER/settings | jq ".settings.charge_current")
        if [ $CHARGECUR_M -eq $MASTER_SOCKET_HARDWIRED ]; then
            printf "$Green Passed $NC LBL=$loadbl_master, Mode=$MODE: Master chargecurrent is limited to socket hardwiring.\n"
        else
            printf "$Red Failed $NC LBL=$loadbl_master, Mode=$MODE: Master chargecurrent is $CHARGECUR_M dA and should be limited to $MASTER_SOCKET_HARDWIRED dA.\n"
        fi
        CHARGECUR_S=$(curl -s -X GET $SLAVE/settings | jq ".settings.charge_current")
        if [ $CHARGECUR_S -eq $SLAVE_SOCKET_HARDWIRED ]; then
            printf "$Green Passed $NC LBL=$loadbl_slave, Mode=$MODE: Slave chargecurrent is limited to socket hardwiring.\n"
        else
            printf "$Red Failed $NC LBL=$loadbl_slave, Mode=$MODE: Slave chargecurrent is $CHARGECUR_S dA and should be limited to $SLAVE_SOCKET_HARDWIRED dA.\n"
        fi
    done
done

#for all other tests we don't want socket resistors to limit our currents, so switch to Fixed Cable
for device in $MASTER $SLAVE; do
    $CURLPOST $device/automated_testing?config=1
    $CURLPOST $device/automated_testing?config=1
    #TODO do we want to restore all MaxCurrent/MaxCircuit/MaxMains values?
done

exit 0

