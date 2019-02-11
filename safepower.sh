#!/bin/sh
#------------------------------------------------------------------------------
# safepower.sh
# run this script before removing power from the Omega2
# can cancel safepower by running again
#------------------------------------------------------------------------------
echo
echo "  === SafePower ==="
echo

#------------------------------------------------------------------------------
# error and print functions
#------------------------------------------------------------------------------
err() { echo -e "FAILED\n"; exit 1; }
prin() { echo -n "  $1..................................." | head -c 35; }

#------------------------------------------------------------------------------
# get mtdblock of overlay (mtdblock6 in this case)
#------------------------------------------------------------------------------
prin "finding overlay mtdblock"
mtdb="mtdblock[0-9]"
mtdb=$(grep "$mtdb.*/overlay" /proc/mounts | grep -o "$mtdb") || err
echo $mtdb

#------------------------------------------------------------------------------
# toggle between rw/ro, so can toggle safepower mode on/off
# rr is the new state wanted
#------------------------------------------------------------------------------
grep -q "$mtdb.*ro," /proc/mounts && rr="rw" || rr="ro"

#------------------------------------------------------------------------------
# sync (mount should take care of this, but do anyway)
#------------------------------------------------------------------------------
[ $rr == "ro" ] && (
    prin "syncing any open files"
    sync || err
    echo "DONE"
)

#------------------------------------------------------------------------------
# remount as read-only or read-write
#------------------------------------------------------------------------------
prin "remounting $mtdb as $rr"
mount -o $rr,remount /dev/$mtdb || err
grep -q "$mtdb.*$rr," /proc/mounts || err
echo "VERIFIED"

#------------------------------------------------------------------------------
# blink code to signify if SafePower mode is on, or back to default
#------------------------------------------------------------------------------
echo
[ $rr == "ro" ] && (
    echo morse > /sys/class/leds/omega2\:amber\:system/trigger
    echo 150 > /sys/class/leds/omega2\:amber\:system/delay
    echo SP > /sys/class/leds/omega2\:amber\:system/message
    echo "  SafePower is ON - you can now safely remove power"
) || (
    echo default-on > /sys/class/leds/omega2\:amber\:system/trigger
    echo "  SafePower is OFF - running normally"
)
echo

