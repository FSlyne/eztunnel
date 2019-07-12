bash stop_tunnel.sh
sleep 1
./eztunnel -i abc -a -s -p 65001 &
ifconfig abc 1.2.1.1 netmask 255.255.255.0 up
