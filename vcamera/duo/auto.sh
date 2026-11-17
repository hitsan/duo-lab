#!/bin/sh
# Milk-V's /etc/init.d/S99user runs this at boot. Copy to /mnt/data/ on the Duo, with usb-uvc-ncm.sh and vcamera-uvc in /root

# S99user starts its usb.sh in the background just before this; recreating the gadget while it runs would race it
tries=0
while ! ifconfig usb0 2> /dev/null | grep -q 192.168.42.1 && [ $tries -lt 20 ]; do
    sleep 0.5
    tries=$((tries + 1))
done

sh /root/usb-uvc-ncm.sh > /tmp/usb-uvc-ncm.log 2>&1
