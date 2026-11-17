#!/bin/sh
# Expose NCM (ssh) and a 320x240 YUYV 30fps UVC camera as one USB gadget.
# auto.sh runs it at boot, with vcamera-uvc next to it

WIDTH=320
HEIGHT=240
FRAME_INTERVAL=333333    # 100ns units: 30fps
GADGET=/tmp/usb/usb_gadget/cvitek

/etc/run_usb.sh stop
/etc/run_usb.sh probe ncm
/etc/run_usb.sh probe uvc
UVC=$GADGET/functions/uvc.usb1

mkdir -p $UVC/control/header/h
ln -s $UVC/control/header/h $UVC/control/class/fs/h
ln -s $UVC/control/header/h $UVC/control/class/ss/h

FRAME=$UVC/streaming/uncompressed/u/${HEIGHT}p
FRAME_BYTES=$((WIDTH * HEIGHT * 2))
BIT_RATE=$((FRAME_BYTES * 8 * (10000000 / FRAME_INTERVAL)))
mkdir -p $FRAME
echo $WIDTH > $FRAME/wWidth
echo $HEIGHT > $FRAME/wHeight
echo $FRAME_BYTES > $FRAME/dwMaxVideoFrameBufferSize
echo $BIT_RATE > $FRAME/dwMinBitRate
echo $BIT_RATE > $FRAME/dwMaxBitRate
echo $FRAME_INTERVAL > $FRAME/dwDefaultFrameInterval
echo $FRAME_INTERVAL > $FRAME/dwFrameInterval

mkdir $UVC/streaming/header/h
ln -s $UVC/streaming/uncompressed/u $UVC/streaming/header/h/u
for speed in fs hs ss; do
    ln -s $UVC/streaming/header/h $UVC/streaming/class/$speed/h
done

/etc/run_usb.sh start

# f_uvc keeps the gadget off the bus until /dev/video0 is opened, and stalls ep0
# (and with it NCM) until someone answers its control requests: vcamera-uvc does both
sleep 1
$(dirname "$0")/vcamera-uvc > /tmp/vcamera-uvc.log 2>&1 &

# NCM was recreated, so usb0 lost its address
sleep 0.5
ifconfig usb0 192.168.42.1
# dnsmasq answers on the recreated usb0 as is; restart races its own stop and can leave none running
if ! pidof dnsmasq > /dev/null; then
    /etc/init.d/S80dnsmasq start
fi
ifconfig usb0
pidof dnsmasq
