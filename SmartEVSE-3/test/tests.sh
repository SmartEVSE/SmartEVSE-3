#!/bin/bash

# This script is meant to automate testing of SmartEVSEv3 firmware
# Needs jq binary

#serialnrs (4 digits):

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <serialnr_of_master> <serialnr_of_slave> <test_selection> ; serialnr is 4 digits."
    echo "WARNING: ONLY USE THIS SCRIPT ON SMARTEVSEs ON A TEST BENCH"
    echo "NEVER USE THIS SCRIPT ON A LIVE SMARTEVSE; IT _WILL_ BLOW YOUR FUSES AND YOUR BREAKERS!!!"
    exit 1
fi

if [ $3 -eq 0 ]; then #all tests selected
    SEL=$((0xFFFF))
else
    SEL=$3
fi

# please give values in deci-Ampère:
MASTER_SOCKET_HARDWIRED=320
SLAVE_SOCKET_HARDWIRED=130


MASTER="smartevse-"$1".local"
SLAVE="smartevse-"$2".local"

control_c()
# run if user hits control-c
{
    echo -en "\n*** Ouch! Exiting ***\n"
    #kill all running subprocesses
    pkill -P $$
    exit $?
}

# trap keyboard interrupt (control-c)
trap control_c SIGINT


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

init_devices () {
for device in $SLAVE $MASTER; do
    #go to Normal Mode for init
    $CURLPOST $device/reboot
    $CURLPOST $device/automated_testing?loadbl=0
    $CURLPOST $device/settings?mode=1
done
read -p "Make sure all EVSE's are set to NOT CHARGING, then press <ENTER>" dummy
sleep 5
}

init_currents () {
#first load all settings before the test
for device in $SLAVE $MASTER; do
    $CURLPOST $device/automated_testing?config=1
    $CURLPOST $device/automated_testing?current_max=60
    $CURLPOST $device/automated_testing?current_max_circuit=70
    $CURLPOST $device/automated_testing?current_main=80
    $CURLPOST $device/settings?enable_C2=0 #TODO cycle through all tests with different settings !!!
done
}

check_charge_current () {
    CHARGECUR=$(curl -s -X GET $device/settings | jq ".settings.charge_current")
    if [ $CHARGECUR -eq $1 ]; then
        printf "$Green Passed $NC LBL=$loadbl_master, Mode=$MODE: $device chargecurrent is limited to $TESTSTRING.\n"
    else
        printf "$Red Failed $NC LBL=$loadbl_master, Mode=$MODE: $device chargecurrent is $CHARGECUR dA and should be limited to $1 dA because of $TESTSTRING.\n"
    fi
}

check_all_charge_currents () {
    for device in $MASTER $SLAVE; do
        check_charge_current "$TESTVALUE10"
    done
}

set_loadbalancing () {
    #make sure we switch lbl in the right order so we dont get modbus errors
    if [ $loadbl_master -eq 0 ]; then
        loadbl_slave=0
        $CURLPOST $SLAVE/automated_testing?loadbl=$loadbl_slave
        $CURLPOST $MASTER/automated_testing?loadbl=$loadbl_master
    fi
    if [ $loadbl_master -eq 1 ]; then
        $CURLPOST $MASTER/automated_testing?loadbl=$loadbl_master
        loadbl_slave=2
        $CURLPOST $SLAVE/automated_testing?loadbl=$loadbl_slave
    fi
}

set_mode () {
    $CURLPOST $MASTER/settings?mode=$mode_master
    if [ $loadbl_slave -eq 0 ]; then
        $CURLPOST $SLAVE/settings?mode=$mode_master
    fi
    MODE=$(curl -s -X GET $MASTER/settings | jq ".mode")
    printf "Testing  LBL=$loadbl_master, mode=$MODE on $TESTSTRING.\r"
    TESTVALUE10=$((TESTVALUE * 10))
}

#TEST1: MODESWITCH TEST: test if mode changes on master reflects on slave and vice versa
if [ $((SEL & 2**0)) -ne 0 ]; then
    TESTSTRING="Modeswitch"
    printf "Starting $TESTSTRING test:\n"
    $CURLPOST $MASTER/automated_testing?loadbl=1
    $CURLPOST $SLAVE/automated_testing?loadbl=2
    for mode_master in 1 2 3; do
        $CURLPOST $MASTER/settings?mode=$mode_master
        sleep 5
        mode_slave=$(curl -s -X GET $SLAVE/settings | jq ".mode_id")
        if [ "x"$mode_slave == "x"$mode_master ]; then
            printf "$Green Passed $NC Master switching to mode $mode_master, slave follows.\n"
        else
            printf "$Red Failed $NC Master switching to mode $mode_master, slave is at $mode_slave.\n"
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
        printf "Since one of the previous tests failed we abort testing."
        exit $ABORT
    fi
fi

#TEST2: SOCKET HARDWIRING TEST: test if Socket resistors in test bench limit our current // Configuration (0:Socket / 1:Fixed Cable)
#                        needs setting [MASTER|SLAVE]_SOCKET_HARDWIRED to the correct values of your test bench
if [ $((SEL & 2**1)) -ne 0 ]; then
    TESTSTRING="Socket Hardwiring"
    printf "Starting $TESTSTRING test:\n"
    init_devices
    init_currents
    #first load all settings before the test
    for device in $SLAVE $MASTER; do
        $CURLPOST $device/automated_testing?config=0
    done
    read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy

    for loadbl_master in 0 1; do
        set_loadbalancing
        #if we are in loadbl 0 we test the slave device in loadbl 0 also
        for mode_master in 1 2 3; do
            set_mode
            #settle switching modes AND stabilizing charging speeds
            sleep 10
            for device in $SLAVE $MASTER; do
                if [ $device == $MASTER ]; then
                    check_charge_current "$MASTER_SOCKET_HARDWIRED"
                else
                    check_charge_current "$SLAVE_SOCKET_HARDWIRED"
                fi
            done
        done
    done

    #for all other tests we don't want socket resistors to limit our currents, so switch to Fixed Cable
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?config=1
    done
fi

#TEST4: MAXCURRENT TEST: test if MaxCurrent is obeyed
if [ $((SEL & 2**2)) -ne 0 ]; then
    TESTSTRING="MaxCurrent"
    printf "Starting $TESTSTRING test:\n"
    init_devices
    init_currents

    read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy

    for loadbl_master in 0 1; do
        set_loadbalancing
        #if we are in loadbl 0 we test the slave device in loadbl 0 also
        TESTVALUE=12
        for mode_master in 1 3 2; do
            set_mode
            for device in $MASTER $SLAVE; do
                $CURLPOST $device/automated_testing?current_max=$TESTVALUE
            done
            #settle switching modes AND stabilizing charging speeds
            sleep 10
            check_all_charge_currents
            #increase testvalue to test if the device responds to that
            TESTVALUE=$(( TESTVALUE + 1 ))
        done
    done
fi

#TEST8: MAXCIRCUIT TEST: test if MaxCircuit is obeyed
if [ $((SEL & 2**3)) -ne 0 ]; then
    TESTSTRING="MaxCirCuit"
    printf "Starting $TESTSTRING test:\n"
    init_devices
    init_currents
    read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy

    for loadbl_master in 0 1; do
        set_loadbalancing
        #if we are in loadbl 0 we test the slave device in loadbl 0 also
        TESTVALUE=20
        for mode_master in 1 3 2; do
            set_mode
            for device in $MASTER $SLAVE; do
                $CURLPOST $device/automated_testing?current_max_circuit=$TESTVALUE
            done
            #settle switching modes AND stabilizing charging speeds
            sleep 10

            if [ $loadbl_master -eq 0 ]; then
                check_all_charge_currents
            else
                TOTCUR=0
                for device in $SLAVE $MASTER; do
                    CHARGECUR=$(curl -s -X GET $device/settings | jq ".settings.charge_current")
                    TOTCUR=$((TOTCUR + CHARGECUR))
                done
                if [ $TOTCUR -eq $TESTVALUE10 ]; then
                    printf "$Green Passed $NC LBL=$loadbl_master, Mode=$MODE: $device chargecurrent is limited to $TESTSTRING.\n"
                else
                    printf "$Red Failed $NC LBL=$loadbl_master, Mode=$MODE: $device chargecurrent is $CHARGECUR dA and should be limited to $TESTVALUE10 dA because of $TESTSTRING.\n"
                fi
            fi
            #increase testvalue to test if the device responds to that
            TESTVALUE=$(( TESTVALUE + 1 ))
        done
    done
fi

exit 0
