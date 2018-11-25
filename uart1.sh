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
  exit 0
}
trap 'cleanup' INT

#==============================================================================
# setup serial port (append any additional command line parameters)
#==============================================================================
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
    devmem=/root/devmem
    sc=$(( (400000000 / $baud + 5) / 10 ))
    sp=$(( ($sc * 5 + 5) / 10 ))
    $devmem write 0xd24 3          #UART1_HIGHSPEED = 3
    $devmem setbits 0xd0c 0x80     #UART1_LCR.DLAB = 1
    $devmem write 0xd00 1          #UART1_DLL (DLM:DLL = 1)
    $devmem write 0xd04 0          #UART1_DLM
    $devmem clrbits 0xd0c 0x80     #UART1_LCR.DLAB = 1
    $devmem write 0xd28 $sc        #UART1_SAMPLE_COUNT
    $devmem write 0xd2c $sp        #UART1_SAMPLE_POINT
fi

#==============================================================================
# redirect keyboard input to serial port
#==============================================================================
cat > /dev/ttyS1

#==============================================================================
# we are heee until Ctrl+Q
#==============================================================================
