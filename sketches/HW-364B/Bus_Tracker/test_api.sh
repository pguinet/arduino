#!/bin/bash
#
# Script de test de l'API PRIM - Simule l'affichage Arduino
# Lance un appel toutes les 10 secondes pendant 3 minutes
#

# Configuration (mêmes valeurs que Bus_Tracker)
STOP_ID="413248"
LINE_REF="C01252"
STOP_NAME="Marechal Foch"
LINE_NAME="269"
DIRECTION="Garges-Sarcelles RER"

# IMPORTANT: Remplacer par ton token PRIM (https://prim.iledefrance-mobilites.fr/)
API_KEY="VOTRE_TOKEN_PRIM_ICI"
URL="https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring?MonitoringRef=STIF%3AStopPoint%3AQ%3A${STOP_ID}%3A"

# Durée et intervalle
DURATION=180  # 3 minutes
INTERVAL=10   # 10 secondes
ITERATIONS=$((DURATION / INTERVAL))

# Fonction pour formater le temps comme l'Arduino
format_time() {
    local minutes=$1
    local at_stop=$2

    if [ "$at_stop" = "true" ]; then
        echo "A L'ARRET"
    elif [ "$minutes" -eq 0 ]; then
        echo "Imminent"
    elif [ "$minutes" -lt 60 ]; then
        echo "${minutes} min"
    else
        local h=$((minutes / 60))
        local m=$((minutes % 60))
        printf "%dh%02d" $h $m
    fi
}

clear
echo "╔════════════════════════════════════╗"
echo "║        Bus Tracker - Test API      ║"
echo "╚════════════════════════════════════╝"
echo ""

for ((i=1; i<=ITERATIONS; i++)); do
    # Appel API
    BODY=$(curl -s \
        -H "Accept: application/json" \
        -H "apikey: $API_KEY" \
        "$URL")

    NOW=$(date +%s)
    UPDATE_TIME=$(date '+%H:%M')

    # Extraire les 2 premiers bus de la ligne filtrée
    DEPARTURES=$(echo "$BODY" | jq -r --arg line "$LINE_REF" '
        [.Siri.ServiceDelivery.StopMonitoringDelivery[0].MonitoredStopVisit[]? |
        select(.MonitoredVehicleJourney.LineRef.value | contains($line)) |
        {
            time: .MonitoredVehicleJourney.MonitoredCall.ExpectedDepartureTime,
            atStop: .MonitoredVehicleJourney.MonitoredCall.VehicleAtStop
        }] | .[0:2]
    ' 2>/dev/null)

    # Simuler l'écran OLED
    echo "┌────────────────────────────────┐"
    printf "│ %-30s │\n" "$LINE_NAME - $STOP_NAME"
    echo "├────────────────────────────────┤"

    if [ "$DEPARTURES" = "[]" ] || [ -z "$DEPARTURES" ]; then
        printf "│ %-30s │\n" "Aucun bus prevu"
        printf "│ %-30s │\n" ""
    else
        # Parser chaque départ
        for idx in 0 1; do
            DEP=$(echo "$DEPARTURES" | jq ".[$idx]" 2>/dev/null)
            if [ "$DEP" != "null" ] && [ -n "$DEP" ]; then
                TIME_STR=$(echo "$DEP" | jq -r '.time' 2>/dev/null)
                AT_STOP=$(echo "$DEP" | jq -r '.atStop' 2>/dev/null)

                if [ "$TIME_STR" != "null" ] && [ -n "$TIME_STR" ]; then
                    # Parser ISO 8601 et calculer minutes
                    DEP_EPOCH=$(date -d "$TIME_STR" +%s 2>/dev/null)
                    if [ -n "$DEP_EPOCH" ]; then
                        MINUTES=$(( (DEP_EPOCH - NOW) / 60 ))
                        [ $MINUTES -lt 0 ] && MINUTES=0
                        DISPLAY=$(format_time $MINUTES "$AT_STOP")
                        printf "│ %-30s │\n" "$DISPLAY"
                    else
                        printf "│ %-30s │\n" ""
                    fi
                else
                    printf "│ %-30s │\n" ""
                fi
            else
                printf "│ %-30s │\n" ""
            fi
        done
    fi

    echo "├────────────────────────────────┤"
    printf "│ %-6s %23s │\n" "$UPDATE_TIME" "$DIRECTION"
    echo "└────────────────────────────────┘"
    printf "  Appel %d/%d\n" $i $ITERATIONS

    # Ne pas attendre après le dernier appel
    if [ $i -lt $ITERATIONS ]; then
        sleep $INTERVAL
        # Effacer l'affichage précédent (remonter de 10 lignes)
        printf "\033[10A"
    fi
done

echo ""
echo "=== Test terminé ==="
