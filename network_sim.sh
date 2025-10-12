#!/bin/bash


if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Usage: $0 {good|bad|terrible|variable|off} {latency|loss|both}"
    exit 1
fi

CONDITION="$1"
SIM_TYPE="$2"

case "$CONDITION" in
    good)
        case "$SIM_TYPE" in
            latency)
                echo "Setting: Good connection (20ms latency)"
                sudo dnctl pipe 1 config delay 10ms
                ;;
            loss)
                echo "Setting: Good connection (0% packet loss)"
                sudo dnctl pipe 1 config plr 0
                ;;
            both)
                echo "Setting: Good connection (20ms latency, 0% packet loss)"
                sudo dnctl pipe 1 config delay 10ms plr 0
                ;;
            *)
                echo "Invalid simulation type: $SIM_TYPE"
                echo "Usage: $0 {good|bad|terrible|variable|off} {latency|loss|both}"
                exit 1
                ;;
        esac
        ;;
    bad)
        case "$SIM_TYPE" in
            latency)
                echo "Setting: Bad WiFi (100ms latency)"
                sudo dnctl pipe 1 config delay 50ms
                ;;
            loss)
                echo "Setting: Bad WiFi (2% packet loss)"
                sudo dnctl pipe 1 config plr 0.02
                ;;
            both)
                echo "Setting: Bad WiFi (100ms latency, 2% packet loss)"
                sudo dnctl pipe 1 config delay 50ms plr 0.02
                ;;
            *)
                echo "Invalid simulation type: $SIM_TYPE"
                echo "Usage: $0 {good|bad|terrible|variable|off} {latency|loss|both}"
                exit 1
                ;;
        esac
        ;;
    terrible)
        case "$SIM_TYPE" in
            latency)
                echo "Setting: Terrible (200ms latency)"
                sudo dnctl pipe 1 config delay 100ms
                ;;
            loss)
                echo "Setting: Terrible (5% packet loss)"
                sudo dnctl pipe 1 config plr 0.05
                ;;
            both)
                echo "Setting: Terrible (200ms latency, 5% packet loss)"
                sudo dnctl pipe 1 config delay 100ms plr 0.05
                ;;
            *)
                echo "Invalid simulation type: $SIM_TYPE"
                echo "Usage: $0 {good|bad|terrible|variable|off} {latency|loss|both}"
                exit 1
                ;;
        esac
        ;;
    variable)
        case "$SIM_TYPE" in
            latency)
                echo "Setting: Variable latency (100ms average)"

                sudo dnctl pipe 1 config delay 100ms
                ;;
            loss)
                echo "Setting: Variable (0% packet loss)"

                sudo dnctl pipe 1 config plr 0
                ;;
            both)
                echo "Setting: Variable (100ms average latency, 0% packet loss)"

                sudo dnctl pipe 1 config delay 100ms plr 0
                ;;
            *)
                echo "Invalid simulation type: $SIM_TYPE"
                echo "Usage: $0 {good|bad|terrible|variable|off} {latency|loss|both}"
                exit 1
                ;;
        esac
        ;;
    off)
        echo "Disabling network simulation"
        sudo pfctl -F dummynet
        sudo pfctl -d
        sudo dnctl -q flush
        exit 0
        ;;
    *)
        echo "Usage: $0 {good|bad|terrible|variable|off} {latency|loss|both}"
        exit 1
        ;;
esac


echo "dummynet in proto udp from any to any port 7777 pipe 1" | sudo pfctl -f -
echo "dummynet out proto udp from any port 7777 to any pipe 1" | sudo pfctl -f -
sudo pfctl -e

echo "Network simulation active. Run '$0 off' to disable."
