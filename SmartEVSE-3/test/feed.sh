#!/bin/bash

FEED_MAINS="MainsMeter"
FEED_EV="EVMeter"

if [ $# -eq 0 ]; then
    echo "Usage: $0 4-digit-topic-nr-of-your-smartevse."
    echo ""
    echo "Make sure you have a local mosquitto running that receives"
    echo "the topics from the smartevse you are testing."
    exit 1
fi

TOPIC_NUMBER="$1"
SET_TOPIC_MAINS="SmartEVSE/$TOPIC_NUMBER/Set/$FEED_MAINS"
SET_TOPIC_EV="SmartEVSE/$TOPIC_NUMBER/Set/$FEED_EV"

CHARGECURRENT=0
NR_PHASES=3
STATE=""
OLD_CURRENT=20
ENERGY=0

# --- Temp files to hold latest MQTT messages ---
TMP_DIR=$(mktemp -d)
TMP_CHARGE="$TMP_DIR/charge"
TMP_PHASE="$TMP_DIR/phase"
TMP_STATE="$TMP_DIR/state"

# --- Background MQTT subscribers writing to files ---
mosquitto_sub -h 127.0.0.1 -t "SmartEVSE/$TOPIC_NUMBER/ChargeCurrent" | while read -r line; do echo "$line" > "$TMP_CHARGE"; done &
CHARGE_PID=$!
mosquitto_sub -h 127.0.0.1 -t "SmartEVSE/$TOPIC_NUMBER/NrOfPhases" | while read -r line; do echo "$line" > "$TMP_PHASE"; done &
PHASE_PID=$!
mosquitto_sub -h 127.0.0.1 -t "SmartEVSE/$TOPIC_NUMBER/State" | while read -r line; do echo "$line" > "$TMP_STATE"; done &
STATE_PID=$!

cleanup() {
    echo "Cleaning up..."
    kill "$CHARGE_PID" "$PHASE_PID" "$STATE_PID" 2>/dev/null
    wait "$CHARGE_PID" "$PHASE_PID" "$STATE_PID" 2>/dev/null
    rm -rf "$TMP_DIR"
    exit 0
}
trap cleanup SIGINT SIGTERM

echo "Enter your Baseload current in deci-Ampères:"

while true; do
    # Read latest values from temp files, if available
    [[ -s "$TMP_CHARGE" ]] && read -r CHARGECURRENT < "$TMP_CHARGE"
    [[ -s "$TMP_PHASE" ]] && read -r NR_PHASES < "$TMP_PHASE"
    [[ -s "$TMP_STATE" ]] && read -r STATE < "$TMP_STATE"

    # Get user input or fallback to previous
    read -t 0.5 CURRENT
    if [ -z "$CURRENT" ]; then
        CURRENT=$OLD_CURRENT
    elif [[ "$CURRENT" =~ ^-?[0-9]+$ ]]; then
        OLD_CURRENT=$CURRENT
    else
        echo "Invalid input. Please enter a numeric value."
        continue
    fi

    # Adjust charging current if not charging
    if [ "$STATE" != "Charging" ]; then
        CHARGECURRENT=0
    fi

    ADJUSTED_CURRENT=$(( CURRENT + CHARGECURRENT ))

    if [ "$NR_PHASES" -eq 1 ]; then
        PAYLOAD_MAINS="$ADJUSTED_CURRENT:$CURRENT:$CURRENT"
    else
        PAYLOAD_MAINS="$ADJUSTED_CURRENT:$ADJUSTED_CURRENT:$ADJUSTED_CURRENT"
    fi

    echo "$FEED_MAINS-CURRENT=$ADJUSTED_CURRENT (baseload: $CURRENT, charge: $CHARGECURRENT, phases: $NR_PHASES, state: $STATE)"
    mosquitto_pub -h 127.0.0.1 -t "$SET_TOPIC_MAINS" -m "$PAYLOAD_MAINS"

    # Calculate power in Watts (current in deci-Amps, so divide by 10)
    POWER=$(( CHARGECURRENT * NR_PHASES * 230 / 10 ))

    echo "$FEED_EV-CURRENT=$CHARGECURRENT ENERGY=$ENERGY POWER=$POWER"
    mosquitto_pub -h 127.0.0.1 -t "$SET_TOPIC_EV" -m "$CHARGECURRENT:$CHARGECURRENT:$CHARGECURRENT:$POWER:$ENERGY"

    # Increase energy only if charging
    if [ "$STATE" = "Charging" ]; then
        ENERGY=$((ENERGY + 100))
    fi

    sleep 1
done

