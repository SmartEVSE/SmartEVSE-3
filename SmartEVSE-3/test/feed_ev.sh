FEED="EVMeter"

if [ $# -eq 0 ]; then
    echo "Usage: $0 4-digit-topic-nr-of-your-smartevse."
    exit 1
fi

echo "Enter your current in deci-Ampères:"
OLD_CURRENT=20
#energy in Wh:
ENERGY=0
while true; do
    read -t 5 CURRENT
    if [ $CURRENT"x" == "x" ]; then
        CURRENT=$OLD_CURRENT
    else
        OLD_CURRENT=$CURRENT
    fi
    echo $FEED-CURRENT=$CURRENT.
    mosquitto_pub  -h 127.0.0.1 -t "SmartEVSE-$1/Set/$FEED" -m $CURRENT:$CURRENT:$CURRENT:11000:$ENERGY
    ENERGY=$((ENERGY + 100))
done
