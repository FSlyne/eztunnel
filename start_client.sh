bash stop_tunnel.sh
./eztunnel -i abc -a -c 192.168.0.72 -p 65001 &
ifconfig abc 1.2.1.2 netmask 255.255.255.0 up
