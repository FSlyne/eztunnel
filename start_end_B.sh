bash stop_tunnel.sh
sleep 1
./eztunnel -i abc -a -r 192.168.0.72 -p 65001 &
ifconfig abc 1.2.1.2 netmask 255.255.255.0 up
