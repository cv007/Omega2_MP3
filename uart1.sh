#!/bin/sh

#interactive UART1 from ssh session

#==============================================================================
# usage
#==============================================================================
usage() {
    echo
    [ "$1" ] && echo "$1"
    echo
    echo "Usage:"
    echo "  uart1.sh <baud> [ <stty-options> ... ]"
    echo "  Example: uart1 115200"
    echo "  Press Ctrl+Q to quit"
    echo
    exit 1
}

#==============================================================================
# check for valid baud values
# standard vlaues from 300-115200 and anything from 115200-600000
#==============================================================================
check_baud() {
    [ $1 -eq 0 ] && usage "invalid baud value"
    [ $1 -gt 600000 ] && usage "max baud is 600000"
    [ $1 -gt 115200 ] && return
    for n in 110 300 600 1200 2400 4800 9600 19200 38400 57600 115200; do
        [ $1 -eq $n ] && return
    done
    usage "non-standard baud value"
}

#==============================================================================
# check script parameters
# convert baud string to integer
# verify valid baud values
#==============================================================================
[ "$1" ] || usage
baud=$(printf '%d' $(( $1 )) ) 2>/dev/null
check_baud $baud
shift
set -e #exit on failures
devmem=/root/devmem

#==============================================================================
# save settings of current terminal to restore later
#==============================================================================
save_stty="$(stty -g)"

#==============================================================================
# trap INT to cleanup after ctrl-q
#==============================================================================
cleanup() {
  kill -9 $catpid &>/dev/null
  set +e
  stty $save_stty
  #UART1_HIGHSPEED = 0
  $devmem w 0xd24 0
  exit 0
}
trap 'cleanup' INT

#==============================================================================
# setup serial port (append any additional command line parameters)
#==============================================================================
#UART1_HIGHSPEED = 0 (in case is still at 3 for some reason)
$devmem w 0xd24 0
stty -F /dev/ttyS1 raw -echo "$@"

#==============================================================================
# set current terminal to pass through everything except Ctrl+Q
# "quit undef susp undef" will disable Ctrl+\ and Ctrl+Z handling
# "isig intr ^Q" will make Ctrl+Q send SIGINT to this script
#==============================================================================
stty raw -echo isig intr ^Q quit undef susp undef

#==============================================================================
# cat reads the serial port to the screen in the background
# get PID of background process so can be terminated in cleanup
#==============================================================================
cat /dev/ttyS1 &
catpid=$!

#==============================================================================
# for high speed baud rates (>115200)
# write to highspeed register, set sample count and sample period registers
#==============================================================================
if [ $baud -gt 115200 ]; then
    #Omega2 registers, bitmasks, clock
    UART1_IER=0xd04; ERFBIbm=0x01
    UART1_LCR=0xd0c; DLABbm=0x80
    UART1_DLL=0xd00; UART1_DLM=0xd04
    UART1_HIGHSPEED=0xd24
    UART1_SAMPLE_COUNT=0xd28; UART1_SAMPLE_POINT=0xd2c
    SYSTEM_CLOCK=40000000

    #compute count, round up
    sc=$(( ($SYSTEM_CLOCK * 10 / $baud + 5) / 10 ))
    #sample = count/2, round up
    sp=$(( ($sc * 5 + 5) / 10 ))

    #disable rx buffer irq, set highspeed to 3,
    #set dlab then write to dlm:dll, clear dlab
    #write count and sample, enable rx buffer irq
    #send all commands to devmem (devmem runs just once)
    $(echo -n "$devmem \
        c $UART1_IER $ERFBIbm
        w $UART1_HIGHSPEED 3 \
        s $UART1_LCR $DLABbm \
        w $UART1_DLL 1 \
        w $UART1_DLM 0 \
        c $UART1_LCR $DLABbm \
        w $UART1_SAMPLE_COUNT $sc \
        w $UART1_SAMPLE_POINT $sp \
        s $UART1_IER $ERFBIbm \
    ")
fi

#==============================================================================
# redirect keyboard input to serial port
#==============================================================================
cat > /dev/ttyS1

#==============================================================================
# we are heee until Ctrl+Q
#==============================================================================
