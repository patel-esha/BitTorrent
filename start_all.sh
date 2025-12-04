#!/bin/bash

echo "Starting all 9 peers..."

(cd peer_1001 && ./peerProcess 1001) &
(cd peer_1002 && ./peerProcess 1002) &
(cd peer_1003 && ./peerProcess 1003) &
(cd peer_1004 && ./peerProcess 1004) &
(cd peer_1005 && ./peerProcess 1005) &
(cd peer_1006 && ./peerProcess 1006) &
(cd peer_1007 && ./peerProcess 1007) &
(cd peer_1008 && ./peerProcess 1008) &
(cd peer_1009 && ./peerProcess 1009) &

echo "All peers started!"
echo "Wait 10-20 seconds for completion..."
echo "Check logs: tail -f 1002/log_peer_*.log"
echo "To stop all: pkill peerProcess"

wait
