#!/bin/bash

# This script is meant to automate testing of SmartEVSEv3 firmware
# Needs jq binary

#serialnrs (4 digits):

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <serialnr_of_master> <serialnr_of_slave> <test_selection> ; serialnr is 4 digits."
    echo
    echo "Make sure you compiled your test version with -DAUTOMATED_TESTING=1 !!"
    echo
    echo "Known BUGS: Only run one test at a time; running multiple tests leads to false <fails>"
    echo
    echo "WARNING: ONLY USE THIS SCRIPT ON SMARTEVSEs ON A TEST BENCH"
    echo "NEVER USE THIS SCRIPT ON A LIVE SMARTEVSE; IT _WILL_ BLOW YOUR FUSES AND YOUR BREAKERS!!!"
    exit 1
fi

COUNT=`pgrep tests | wc -l`
if [ $COUNT -ne 2 ]; then
    echo "Tests are already running in the background, exiting"
    killall tests.sh
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
MASTER_MAC_ID=$1
SLAVE_MAC_ID=$2

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
CURLPOST="curl -s -o /dev/null -X POST -d''"

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
    sleep 1
    $CURLPOST "$device/automated_testing?loadbl=0&mainsmeter=1"
    $CURLPOST $device/settings?mode=1
done
read -p "Make sure all EVSE's are set to NOT CHARGING, then press <ENTER>" dummy
sleep 5
}

init_currents () {
#first load all settings before the test
for device in $SLAVE $MASTER; do
    $CURLPOST "$device/automated_testing?config=1&current_max=60&current_max_circuit=70&current_main=80"
    $CURLPOST "$device/settings?current_max_sum_mains=600&enable_C2=0"
    #$CURLPOST $device/settings?enable_C2=0 #TODO cycle through all tests with different settings !!!
done
}

#takes 3 arguments, actual value, test value and margin
print_results() {
    if [ $DBG -eq 1 ]; then
        printf "CHARGECUR=$1, TARGET=$2."
    fi
    if [ $1 -ge $(( $2 - $3 )) ] && [ $1 -le $(( $2 + $3 )) ]; then
        printf "$Green Passed $NC LBL=$loadbl_master, Mode=$MODE: $device chargecurrent is limited to $TESTSTRING.\n"
    else
        printf "$Red Failed $NC LBL=$loadbl_master, Mode=$MODE: $device chargecurrent is $1 dA and should be limited to $2 dA (with a margin of $3 dA) because of $TESTSTRING.\n"
    fi
}

#takes 4 arguments, actual value, test value, margin, string_to_test
print_results2() {
    if [ $DBG -eq 1 ]; then
        printf "$4=$1, TARGET=$2."
    fi
    if [ $1 -ge $(( $2 - $3 )) ] && [ $1 -le $(( $2 + $3 )) ]; then
        printf "$Green Passed $NC LBL=$loadbl_master, Mode=$MODE: $device $4 is as expected when testing $TESTSTRING.\n"
    else
        printf "$Red Failed $NC LBL=$loadbl_master, Mode=$MODE: $device $4=$1 should be $4=$2 when testing $TESTSTRING.\n"
    fi
}

check_all_charge_currents () {
    for device in $MASTER $SLAVE; do
        check_charging
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
        sleep 1
        loadbl_slave=2
        $CURLPOST $SLAVE/automated_testing?loadbl=$loadbl_slave
    fi
}

MODESTR=("Off" "Normal" "Solar" "Smart")

set_mode () {
    $CURLPOST $MASTER/settings?mode=$mode_master
    if [ $loadbl_slave -eq 0 ]; then
        $CURLPOST $SLAVE/settings?mode=$mode_master
    fi
    MODE=${MODESTR[$mode_master]}
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
            check_charging
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
    device=$MASTER
    set_mainsmeter_to_api
    loadbl_master=1
    set_loadbalancing
    read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy
    #if we are in loadbl 0 we don't test the slave device
    for mode_master in 3 2; do
        set_mode
        $CURLPOST $device$CONFIG_COMMAND
        overload_mains
        TOTCUR=0
        for device in $SLAVE $MASTER; do
            check_charging
            TOTCUR=$((TOTCUR + CHARGECUR))
        done
        #we started charging at maxcurrent and then stepped down for approx. 1A per 670ms
        print_results "$TOTCUR" "${TARGET[$mode_master]}" "$MARGIN"
    done
    #set MainsMeter to Sensorbox
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?mainsmeter=1
    done
    #kill all running subprocesses
    pkill -P $$
}

check_charging () {
    #make sure we are actually charging
    CURL=$(curl -s -X GET $device/settings)
    STATE_ID=$(echo $CURL | jq ".evse.state_id")
    print_results2 "$STATE_ID" "2" "0" "STATE_ID"
    CHARGECUR=$(echo $CURL | jq ".settings.charge_current")
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
                check_charging
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
                    check_charging
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
    TARGET=(0 0 420 560)
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

#TEST256: STARTCURRENT TEST: test if StartCurrent is obeyed in Solar Mode for loadbl=0
NR=$((2**8))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="StartCurrent, StopTimer and ImportCurrent via EM_API for loadbl=0"
    printf "Starting $TESTSTRING test #$NR:\n"
    #the margin for which we will accept the lowering/upping of the charge current, in dA
    MARGIN=20
    #make mains_overload feed mains_current with 3A to the grid
    TESTVALUE=-3
    #note that startcurrent shown as -4A on the display is stored as 4A !
    #CONFIG_COMMAND="/settings?solar_start_current=4"
    init_devices
    init_currents
    for device in $MASTER $SLAVE; do
        set_mainsmeter_to_api
        $CURLPOST "$device/settings?solar_start_current=4&solar_max_import=15&solar_stop_time=1"
    done
    read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy

    loadbl_master=0
    set_loadbalancing
    #SOLAR mode
    mode_master=2
    set_mode
    for device in $MASTER $SLAVE; do
        echo 60 >feed_mains_$device
    done
    printf "Feeding total of 18A....chargecurrent should drop to 6A, then triggers stoptimer and when it expires, stops charging because over import limit of 15A\r"
    TESTSTRING="SolarStopTimer should have been activated on overload on ImportCurrent"
    sleep 50
    for device in $MASTER $SLAVE; do
        TIMER=$(curl -s -X GET $device/settings | jq ".evse.solar_stop_timer")
        print_results2 "$TIMER" "28" "15" "SOLAR_STOP_TIMER"
    done
    TESTSTRING="Charging should stop after expiring SolarStopTimer"
    printf "$TESTSTRING\r"
    sleep 40
    for device in $MASTER $SLAVE; do
        STATE_ID=$(curl -s -X GET $device/settings | jq ".evse.state_id")
        print_results2 "$STATE_ID" "10" "0" "STATE_ID"
        echo -20 >feed_mains_$device
    done
    TESTSTRING="Feeding total of -6A....should trigger ready-timer 60s"
    printf "$TESTSTRING\r"
    sleep 65
    read -p "To start charging, set EVSE's to NO CHARGING and then to CHARGING again, then press <ENTER>" dummy
    TESTSTRING="Feeding total of 18A should drop the charging current"
    printf "$TESTSTRING\r"
    for device in $MASTER $SLAVE; do
        check_charging
        #dropping the charge current by a few amps
        echo 60 >feed_mains_$device
    done
    sleep 3
    TESTSTRING="Feeding total of 15A should stabilize the charging current"
    printf "$TESTSTRING\r"
    for device in $MASTER $SLAVE; do
        check_charging
        print_results "$CHARGECUR" "475" "30"
        echo 50 >feed_mains_$device
    done
    sleep 10
    for device in $MASTER $SLAVE; do
        check_charging
        print_results "$CHARGECUR" "450" "35"
    done
    #set MainsMeter to Sensorbox
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?mainsmeter=1
    done
    #kill all running subprocesses
    pkill -P $$
fi

#TEST512: STARTCURRENT TEST: test if StartCurrent is obeyed in Solar Mode for loadbl=1
NR=$((2**9))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="StartCurrent, StopTimer and ImportCurrent via EM_API for loadbl=1"
    printf "Starting $TESTSTRING test #$NR:\n"
    #the margin for which we will accept the lowering/upping of the charge current, in dA
    MARGIN=20
    #make mains_overload feed mains_current with 3A to the grid
    TESTVALUE=-3
    #note that startcurrent shown as -4A on the display is stored as 4A !
    #CONFIG_COMMAND="/settings?solar_start_current=4"
    init_devices
    init_currents
    for device in $MASTER; do
        set_mainsmeter_to_api
        $CURLPOST "$device/settings?solar_start_current=4&solar_max_import=15&solar_stop_time=1"
    done

    loadbl_master=1
    set_loadbalancing
    #SOLAR mode
    sleep 2
    mode_master=2
    set_mode
    sleep 2
    printf "\n"
    read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy
    TESTSTRING="Feeding total of 3A so we should wait for the sun"
    printf "$TESTSTRING\r"
    echo 10 >feed_mains_$MASTER
    sleep 5
    for device in $MASTER $SLAVE; do
        STATE_ID=$(curl -s -X GET $device/settings | jq ".evse.state_id")
        print_results2 "$STATE_ID" "9" "0" "STATE_ID"
    done
    TESTSTRING="Feeding total of -6A....should trigger ready-timer 60s"
    printf "$TESTSTRING\r"
    echo -20 >feed_mains_$MASTER
    sleep 70
    read -p "To start charging, set EVSE's to NO CHARGING and then to CHARGING again, then press <ENTER>" dummy
    sleep 2
    for device in $MASTER $SLAVE; do
        check_charging
    done
    TESTSTRING="Feeding total of 18A should drop the charging current"
    printf "$TESTSTRING\r"
    echo 60 >feed_mains_$MASTER
    sleep 10
    for device in $MASTER $SLAVE; do
        check_charging
        print_results "$CHARGECUR" "225" "20"
    done
    TESTSTRING="Feeding total of 15A should stabilize the charging current"
    printf "$TESTSTRING\r"
    echo 50 >feed_mains_$MASTER
    sleep 10
    for device in $MASTER $SLAVE; do
        check_charging
        print_results "$CHARGECUR" "210" "20"
    done
    printf "Feeding total of 18A....chargecurrent should drop to 6A, then triggers stoptimer and when it expires, stops charging because over import limit of 15A\r"
    TESTSTRING="SolarStopTimer should have been activated on overload on ImportCurrent"
    echo 60 >feed_mains_$MASTER
    sleep 60
    for device in $MASTER $SLAVE; do
        TIMER=$(curl -s -X GET $device/settings | jq ".evse.solar_stop_timer")
        print_results2 "$TIMER" "15" "5" "SOLAR_STOP_TIMER"
    done
    TESTSTRING="Charging should stop after expiring SolarStopTimer"
    printf "$TESTSTRING\r"
    sleep 40
    for device in $MASTER $SLAVE; do
        STATE_ID=$(curl -s -X GET $device/settings | jq ".evse.state_id")
        print_results2 "$STATE_ID" "9" "0" "STATE_ID"
    done
    TESTSTRING="Feeding total of -6A....should trigger ready-timer 60s"
    printf "$TESTSTRING\r"
    echo -20 >feed_mains_$MASTER
    sleep 63
    read -p "To start charging, set EVSE's to NO CHARGING and then to CHARGING again, then press <ENTER>" dummy
    sleep 2
    for device in $MASTER $SLAVE; do
        check_charging
    done
    #set MainsMeter to Sensorbox
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?mainsmeter=1
    done
    #kill all running subprocesses
    pkill -P $$
fi

#TEST1024: shortened version of test512
NR=$((2**10))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="StartCurrent, StopTimer and ImportCurrent via EM_API for loadbl=1"
    printf "Starting $TESTSTRING test #$NR:\n"
    #the margin for which we will accept the lowering/upping of the charge current, in dA
    MARGIN=20
    TESTVALUE=-3
    TESTVALUE10=$((TESTVALUE*10))
    #note that startcurrent shown as -4A on the display is stored as 4A !
    #CONFIG_COMMAND="/settings?solar_start_current=4"
    init_devices
    init_currents
    for device in $MASTER; do
        set_mainsmeter_to_api
        $CURLPOST "$device/settings?solar_start_current=4&solar_max_import=15&solar_stop_time=1"
    done
    #to speed up testing lower max_current
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?current_max=9
    done
    sleep 1
    loadbl_master=1
    set_loadbalancing
    #SOLAR mode
    sleep 2
    mode_master=2
    set_mode
    sleep 2
    printf "\n"
    read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy
    printf "Feeding total of 18A....chargecurrent should drop to 6A, then triggers stoptimer and when it expires, stops charging because over import limit of 15A\r"
    TESTSTRING="SolarStopTimer should have been activated on overload on ImportCurrent"
    echo 60 >feed_mains_$MASTER
    sleep 40
    for device in $MASTER $SLAVE; do
        TIMER=$(curl -s -X GET $device/settings | jq ".evse.solar_stop_timer")
        print_results2 "$TIMER" "26" "5" "SOLAR_STOP_TIMER"
    done
    TESTSTRING="Charging should stop after expiring SolarStopTimer"
    printf "$TESTSTRING\r"
    sleep 40
    for device in $MASTER $SLAVE; do
        STATE_ID=$(curl -s -X GET $device/settings | jq ".evse.state_id")
        print_results2 "$STATE_ID" "9" "0" "STATE_ID"
    done
    TESTSTRING="Feeding total of -6A....should trigger ready-timer 60s"
    printf "$TESTSTRING\r"
    echo -20 >feed_mains_$MASTER
    sleep 63
    read -p "To start charging, set EVSE's to NO CHARGING and then to CHARGING again, then press <ENTER>" dummy
    sleep 2
    for device in $MASTER $SLAVE; do
        check_charging
    done
    #set MainsMeter to Sensorbox
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?mainsmeter=1
    done
    #kill all running subprocesses
    pkill -P $$
fi

#TEST2048: modified version of test1024, only testing a master without any slaves
NR=$((2**11))
if [ $((SEL & NR)) -ne 0 ]; then
    TESTSTRING="StartCurrent, StopTimer and ImportCurrent via EM_API for loadbl=1 with only Master"
    printf "Starting $TESTSTRING test #$NR:\n"
    #the margin for which we will accept the lowering/upping of the charge current, in dA
    MARGIN=20
    #make mains_overload feed mains_current with 3A per phase to the grid
    TESTVALUE=-3
    TESTVALUE10=$((TESTVALUE*10))
    #note that startcurrent shown as -4A on the display is stored as 4A !
    #CONFIG_COMMAND="/settings?solar_start_current=4"
    init_devices
    init_currents
    for device in $MASTER; do
        set_mainsmeter_to_api
        $CURLPOST "$device/settings?solar_start_current=4&solar_max_import=15&solar_stop_time=1"
    done
    #to speed up testing lower max_current
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?current_max=9
    done
    loadbl_master=1
    set_loadbalancing
    #put slave into multi=disabled
    $CURLPOST $SLAVE/automated_testing?loadbl=0
    #SOLAR mode
    sleep 2
    mode_master=2
    set_mode
    sleep 2
    printf "\n"
    read -p "Make sure all EVSE's are set to CHARGING, then press <ENTER>" dummy
    printf "Feeding total of 18A....chargecurrent should drop to 6A, then triggers stoptimer and when it expires, stops charging because over import limit of 15A\r"
    TESTSTRING="SolarStopTimer should have been activated on overload on ImportCurrent"
    echo 60 >feed_mains_$MASTER
    sleep 65
    for device in $MASTER; do
        TIMER=$(curl -s -X GET $device/settings | jq ".evse.solar_stop_timer")
        print_results2 "$TIMER" "8" "5" "SOLAR_STOP_TIMER"
    done
    TESTSTRING="Charging should stop after expiring SolarStopTimer"
    printf "$TESTSTRING\r"
    sleep 40
    for device in $MASTER; do
        STATE_ID=$(curl -s -X GET $device/settings | jq ".evse.state_id")
        print_results2 "$STATE_ID" "9" "0" "STATE_ID"
    done
    TESTSTRING="Feeding total of -6A....should trigger ready-timer 60s"
    printf "$TESTSTRING\r"
    echo -20 >feed_mains_$MASTER
    sleep 63
    read -p "To start charging, set EVSE's to NO CHARGING and then to CHARGING again, then press <ENTER>" dummy
    sleep 2
    for device in $MASTER; do
        check_charging
    done
    #set MainsMeter to Sensorbox
    for device in $MASTER $SLAVE; do
        $CURLPOST $device/automated_testing?mainsmeter=1
    done
    #kill all running subprocesses
    pkill -P $$
fi

exit 0
