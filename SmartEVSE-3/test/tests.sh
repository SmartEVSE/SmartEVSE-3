#!/bin/bash

# This script is meant to automate testing of SmartEVSEv3 firmware
# Needs jq binary

#serialnrs (4 digits):

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <serialnr_of_master> <serialnr_of_slave> <test_selection> ; serialnr is 4 digits."
    echo
    echo "Make sure you compiled your test version with -DAUTOMATED_TESTING=1 !!"
    echo
    echo "WARNING: ONLY USE THIS SCRIPT ON SMARTEVSEs ON A TEST BENCH"
    echo "NEVER USE THIS SCRIPT ON A LIVE SMARTEVSE; IT _WILL_ BLOW YOUR FUSES AND YOUR BREAKERS!!!"
    exit 1
fi

if [ $3 -eq 0 ]; then #all tests selected
    SEL=$((0xFFFF))
else
    SEL=$3
fi

DBG=1 #1 means debug mode on, 0 means debug mode off

# please give values in deci-Ampère:
MASTER_SOCKET_HARDWIRED=320
SLAVE_SOCKET_HARDWIRED=130
MASTER_MAC_ID=5700
SLAVE_MAC_ID=45327

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
    $CURLPOST $device/automated_testing?mainsmeter=1
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
    $CURLPOST $device/settings?current_max_sum_mains=600
    $CURLPOST $device/settings?enable_C2=0 #TODO cycle through all tests with different settings !!!
done
}

#takes 3 arguments, actual value, test value and margin
print_results() {
    if [ $DBG -eq 1 ]; then
        printf "CHARGECUR=$1, TARGET=$2."
    fi
    if [ $1 -ge $(( $2 - $3 )) ] && [ $CHARGECUR -le $(( $2 + $3 )) ]; then
        printf "$Green Passed $NC LBL=$loadbl_master, Mode=$MODE: $device chargecurrent is limited to $TESTSTRING.\n"
    else
        printf "$Red Failed $NC LBL=$loadbl_master, Mode=$MODE: $device chargecurrent is $1 dA and should be limited to $2 dA (with a margin of $3 dA) because of $TESTSTRING.\n"
    fi
}

check_all_charge_currents () {
    for device in $MASTER $SLAVE; do
        CHARGECUR=$(curl -s -X GET $device/settings | jq ".settings.charge_current")
        print_results "$CHARGECUR" "$TESTVALUE10" "0"
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

overload_mains () {
#    echo $TESTVALUE10 >feed_mains_$device
    #now overload the mains by 1A
    echo $(( TESTVALUE10 + 10 )) >feed_mains_$device
    #settle switching modes AND stabilizing charging speeds
    printf "Watch the charge current of device $device going down in 1-2A steps!\r"
    sleep 10
    #now stabilize the mains to MaxMains
    echo $(( TESTVALUE10 )) >feed_mains_$device
}

set_mainsmeter_to_api () {
    if [ ! -e "feed_mains_$device" ]; then
        mkfifo feed_mains_$device
    fi
    if [ $device == $MASTER ]; then
        MAC_ID=$MASTER_MAC_ID
    else
        MAC_ID=$SLAVE_MAC_ID
    fi
    ./feed_mains.sh $MAC_ID <feed_mains_$device >/dev/null &
    echo $TESTVALUE10 >feed_mains_$device
    $CURLPOST $device/automated_testing?mainsmeter=9
}

run_test_loadbl0 () {
    init_devices
    init_currents
    for device in $MASTER $SLAVE; do
        set_mainsmeter_to_api
    done
    read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy

    loadbl_master=0
    set_loadbalancing
    for mode_master in 3 2; do
        set_mode
        for device in $MASTER $SLAVE; do
            $CURLPOST $device$CONFIG_COMMAND
            overload_mains
            CHARGECUR=$(curl -s -X GET $device/settings | jq ".settings.charge_current")
            #we start charging at maxcurrent and then step down for approx. 1A per 670ms
            print_results "$CHARGECUR" "${TARGET[$mode_master]}" "$MARGIN"
        done
    done
    #set MainsMeter to Sensorbox
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?mainsmeter=1
    done
    #kill all running subprocesses
    pkill -P $$
}

run_test_loadbl1 () {
    init_devices
    init_currents
    for device in $MASTER; do
        set_mainsmeter_to_api
    done
    for loadbl_master in 1; do
        set_loadbalancing
        read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy
        #if we are in loadbl 0 we don't test the slave device
        for mode_master in 3 2; do
            set_mode
            for device in $MASTER; do
                $CURLPOST $device$CONFIG_COMMAND
                overload_mains
                TOTCUR=0
                for device in $SLAVE $MASTER; do
                    CHARGECUR=$(curl -s -X GET $device/settings | jq ".settings.charge_current")
                    TOTCUR=$((TOTCUR + CHARGECUR))
                done
                #we started charging at maxcurrent and then stepped down for approx. 1A per 670ms
                print_results "$TOTCUR" "${TARGET[$mode_master]}" "$MARGIN"
            done
        done
    done
    #set MainsMeter to Sensorbox
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?mainsmeter=1
    done
    #kill all running subprocesses
    pkill -P $$
}

#TEST1: MODESWITCH TEST: test if mode changes on master reflects on slave and vice versa
NR=$((2**0))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="Modeswitch"
    printf "Starting $TESTSTRING test #$NR:\n"
    init_devices
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
NR=$((2**1))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="Socket Hardwiring"
    printf "Starting $TESTSTRING test #$NR:\n"
    printf "Make sure your Sensorbox is on MAX power delivery to the grid, or Solar tests will fail!\n"
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
                CHARGECUR=$(curl -s -X GET $device/settings | jq ".settings.charge_current")
                if [ $device == $MASTER ]; then
                    print_results "$CHARGECUR" "$MASTER_SOCKET_HARDWIRED" "0"
                else
                    print_results "$CHARGECUR" "$SLAVE_SOCKET_HARDWIRED" "0"
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
NR=$((2**2))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="MaxCurrent"
    printf "Starting $TESTSTRING test #$NR:\n"
    printf "Make sure your Sensorbox is on MAX power delivery to the grid, or Solar tests will fail!\n"
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
NR=$((2**3))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="MaxCirCuit"
    printf "Starting $TESTSTRING test #$NR:\n"
    printf "Make sure your Sensorbox is on MAX power delivery to the grid, or Solar tests will fail!\n"
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
            sleep 13

            if [ $loadbl_master -eq 0 ]; then
                check_all_charge_currents
            else
                TOTCUR=0
                for device in $SLAVE $MASTER; do
                    CHARGECUR=$(curl -s -X GET $device/settings | jq ".settings.charge_current")
                    TOTCUR=$((TOTCUR + CHARGECUR))
                done
                print_results "$TOTCUR" "$TESTVALUE10" "0"
            fi
            #increase testvalue to test if the device responds to that
            TESTVALUE=$(( TESTVALUE + 1 ))
        done
    done
fi

#TEST16: MAXMAINS TEST: test if MaxMains is obeyed when using EM_API for loadbl=0
NR=$((2**4))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="MaxMains via EM_API for loadbl=0"
    printf "Starting $TESTSTRING test #$NR:\n"
    #the margin for which we will accept the lowering/upping of the charge current, in dA
    MARGIN=20
    TESTVALUE=25
    #Target values for Off, Normal, Solar, Smart mode RESPECTIVELY:
    TARGET=(0 0 300 450)
    CONFIG_COMMAND="/automated_testing?current_main=$TESTVALUE"
    run_test_loadbl0
fi

#TEST32: MAXMAINS TEST: test if MaxMains is obeyed when using EM_API
NR=$((2**5))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="MaxMains via EM_API for loadbl=1"
    printf "Starting $TESTSTRING test #$NR:\n"
    #the margin for which we will accept the lowering/upping of the charge current, in dA
    MARGIN=20
    TESTVALUE=25
    #Target values for Off, Normal, Solar, Smart mode RESPECTIVELY:
    TARGET=(0 0 425 560)
    CONFIG_COMMAND="/automated_testing?current_main=$TESTVALUE"
    run_test_loadbl1
fi

#TEST64: MAXSUMMAINS TEST: test if MaxSumMains is obeyed when using EM_API for loadbl=0
NR=$((2**6))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="MaxSumMains via EM_API for loadbl=0"
    printf "Starting $TESTSTRING test #$NR:\n"
    #the margin for which we will accept the lowering/upping of the charge current, in dA
    MARGIN=20
    TESTVALUE=50
    #Target values for Off, Normal, Solar, Smart mode RESPECTIVELY:
    TARGET=(0 0 310 455)
    CONFIG_COMMAND="/settings?current_max_sum_mains=$((TESTVALUE * 3))"
    run_test_loadbl0
fi

#TEST128: MAXSUMMAINS TEST: test if MaxSumMains is obeyed when using EM_API for loadbl=1
NR=$((2**7))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="MaxSumMains via EM_API for loadbl=1"
    printf "Starting $TESTSTRING test #$NR:\n"
    #the margin for which we will accept the lowering/upping of the charge current, in dA
    MARGIN=20
    TESTVALUE=50
    TARGET=(0 0 425 560)
    CONFIG_COMMAND="/settings?current_max_sum_mains=$((TESTVALUE * 3))"
    run_test_loadbl1
fi

exit 0
