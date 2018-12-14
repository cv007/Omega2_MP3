#!/bin/sh

#interactive UART1/2 from ssh session


#==============================================================================
# usage
#==============================================================================
usage() {
    echo
    [ "$1" ] && echo "$1"
    echo
    echo "Usage:"
    echo "  ${script} <1|2> <baud> [ <stty-options> ... ]"
    echo "  Example: uart 1 115200"
    echo "  Press Ctrl+Q to quit"
    echo
    exit 1
}

#==============================================================================
# check for valid baud values
# standard vlaues from 300-115200 and anything from 115200-1000000
#==============================================================================
check_baud() {
    [ $1 -eq 0 ] && usage "invalid baud value"
    [ $1 -gt 1000000 ] && usage "max baud is 1000000"
    [ $1 -gt 115200 ] && return
    for n in 110 300 600 1200 2400 4800 9600 19200 38400 57600 115200; do
        [ $1 -eq $n ] && return
    done
    usage "non-standard baud value"
}

#==============================================================================
# check script parameters
# convert baud string to integer (0 if fails)
# verify valid baud values
#==============================================================================
script="${0/*\//}" #script name only
[ "$2" ] || usage
[ "$1" != "1" ] && [ "$1" != "2" ] && usage "invalid uart number"
uartn=$1
baud=$(printf '%d' $(( $2 )) ) 2>/dev/null
check_baud $baud
shift
set -e #exit on failures

#==============================================================================
# Omega2 registers, bitmasks, clock
# using uart0 as base (uart1=0xd00, uart2=0xe00)
#==============================================================================
UART_BASE=0xc00
UART_IER=$(( $UART_BASE + ($uartn * 0x100) + 0x4 )); ERFBIbm=0x01
UART_LCR=$(( $UART_BASE + ($uartn * 0x100) + 0xc )); DLABbm=0x80
UART_DLL=$(( $UART_BASE + ($uartn * 0x100) + 0x0 ));
UART_DLM=$(( $UART_BASE + ($uartn * 0x100) + 0x4 ))
UART_HIGHSPEED=$(( $UART_BASE + ($uartn * 0x100) + 0x24 ))
UART_SAMPLE_COUNT=$(( $UART_BASE + ($uartn * 0x100) + 0x28 ))
UART_SAMPLE_POINT=$(( $UART_BASE + ($uartn * 0x100) + 0x2c ))
UART_SYSTEM_CLOCK=40000000
devmem=/root/devmem

#==============================================================================
# if using uart2, need to set to gpio16 (rx), gpio17 (rx)
#==============================================================================
#0x1000003c AGPIO_CFG,EPHY_APGIO_EN[4:1]=1111 (default, already set)
#0x10000060 GPIO1_MODE, SPIS_MODE[3:2]=11 (UARTTXD2/gpio16, UARTRXD2/gpio17)
[ $uartn == "2" ] && $devmem s 0x60 0b1100

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
  $devmem w $UART_HIGHSPEED 0
  exit 0
}
trap 'cleanup' INT

#==============================================================================
# reset highspeed in case still on
# setup serial port (append any additional command line parameters)
#==============================================================================
$devmem w $UART_HIGHSPEED 0
stty -F /dev/ttyS$uartn raw -echo "$@"

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
cat /dev/ttyS$uartn &
catpid=$!

#==============================================================================
# for high speed baud rates (>115200)
# write to highspeed register, set sample count and sample period registers
#==============================================================================
if [ $baud -gt 115200 ]; then
    #compute count, round up at 0.5
    sc=$(( ($UART_SYSTEM_CLOCK * 10 / $baud + 5) / 10 ))
    #sample = count/2, round up at 0.5
    sp=$(( ($sc * 5 + 5) / 10 ))

    #disable rx buffer irq, set highspeed to 3,
    #set dlab then write to dlm:dll, clear dlab
    #write count and sample, enable rx buffer irq
    #send all commands to devmem (devmem runs just once)
    $(echo -n "$devmem \
        c $UART_IER $ERFBIbm
        w $UART_HIGHSPEED 3 \
        s $UART_LCR $DLABbm \
        w $UART_DLL 1 \
        w $UART_DLM 0 \
        c $UART_LCR $DLABbm \
        w $UART_SAMPLE_COUNT $sc \
        w $UART_SAMPLE_POINT $sp \
        s $UART_IER $ERFBIbm \
    ")
fi

#==============================================================================
# redirect keyboard input to serial port
#==============================================================================
cat > /dev/ttyS$uartn

#==============================================================================
# we are here until Ctrl+Q
#==============================================================================
