package main
/*
devman.go
devmem for Omega2 - read/write hardware registers via /dev/mem

Go 1.11.2 32bit downloaded/extracted into $HOME/Programming/go/1.11.2
(not installed, so use export below when at bash prompt)

$export GOROOT=$HOME/Programming/go/1.11.2
$export GOPATH=$HOME/Programming/go/work
$export PATH=$PATH:$GOROOT/bin:$GOPATH/bin/linux_386
$export GOARCH=386
$export GOBIN=$GOPATH/bin/linux_386

bash shell at src folder, cross compile for Omega2-

$GOOS=linux GOARCH=mipsle go build -ldflags="-s -w" devmem.go

then copy to Omega2-
$scp devmem root@Omega_SNAP1:/root
*/

import (
    "fmt"
    "os"
    "syscall"
    "unsafe"
    "strconv"
    "path"
)

const REGBASE = 0x10000000

func usage() {
    pnam := path.Base(os.Args[0])
    fmt.Printf("\n%s read|r <addr>\n", pnam)
    fmt.Printf("%s write|w <addr> <val>\n", pnam)
    fmt.Printf("%s setbits|sb <addr> <bitmask>\n", pnam)
    fmt.Printf("%s clrbits|cb <addr> <bitmask>\n", pnam)
    fmt.Printf("  <addr>    = absolute or offset from 0x10000000\n")
    fmt.Printf("  <bitmask> = bits to set or clear\n\n")
    return
}

func memread(mem []byte, addr int, addr_offset int) {
    v := *(*uint32)(unsafe.Pointer(&mem[addr_offset]))
    v3 := uint8(v>>24); v2 := uint8(v>>16); v1 := uint8(v>>8); v0 := uint8(v)
    fmt.Printf(
        "\n" +
        "addr: [0x%08X] val: [0x%08X]\n" +
        "\n" +
        "bit 3 3 2 2 2 2 2 2  2 2 2 1 1 1 1 1  1 1 1 1 1 1\n" +
        "pos 1 0 9 8 7 6 5 4  3 2 1 0 9 8 7 6  5 4 3 2 1 0 9 8  7 6 5 4 3 2 1 0\n" +
        "    ...............  ...............  ...............  ...............\n",
        addr, v)
    fmt.Printf(
        "bin ")
    vb := v
    for i := 0; i < 32; i++ {
        b := "0 "
        if (vb & (1<<31)) != 0 { b = "1 " }
        fmt.Printf("%s", b)
        if (i & 7) == 7 { fmt.Printf(" ") }
        vb <<= 1
    }
    fmt.Printf(
        "\n" +
        "hex %11s0x%02X  %11s0x%02X  %11s0x%02X  %11s0x%02X\n" +
        "dec %15d  %15d  %15d  %15d\n\n",
        " ", v3, " ", v2, " ", v1, " ", v0, v3, v2, v1, v0)
}
func memwrite(mem []byte, addr_offset int, val uint32) {
    *(*uint32)(unsafe.Pointer(&mem[addr_offset])) = uint32(val)
}
func memsetbits(mem []byte, addr_offset int, val uint32) {
    *(*uint32)(unsafe.Pointer(&mem[addr_offset])) |= uint32(val)
}
func memclrbits(mem []byte, addr_offset int, val uint32) {
    *(*uint32)(unsafe.Pointer(&mem[addr_offset])) &^= uint32(val)
}

func main() {
    args := os.Args[1:]
    if len(args) < 2 { usage(); return }
    opt := args[0]
    a, err := strconv.ParseUint(args[1], 0, 32)
    if err != nil { fmt.Printf("invalid address- %s\n", args[1]); return }
    b := uint64(0)
    switch opt {
        case "r":       fallthrough
        case "read":
        case "w":       fallthrough
        case "write":   fallthrough
        case "sb":      fallthrough
        case "setbits": fallthrough
        case "cb":      fallthrough
        case "clrbits":
            if len(args) < 3 { usage(); return }
            b, err = strconv.ParseUint(args[2], 0, 32)
            if err != nil { fmt.Printf("invalid number- %s\n", args[2]); return }
        default: usage(); return
    }

    addr := int(a) &^ 3 | REGBASE   //word aligned, to full address
    val := uint32(b)
    page_addr := addr &^ 0xFFF      //align on page for mmap
    addr_offset := addr - page_addr //offset into mmap page

    f, err := os.OpenFile("/dev/mem", os.O_RDWR|os.O_SYNC, 0666)
    if err != nil { fmt.Printf("failed to open /dev/mem\n"); return }
    defer f.Close()

    mem, err := syscall.Mmap(int(f.Fd()), int64(page_addr), 4096,
        syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_SHARED)
    if err != nil { fmt.Printf("failed to mmap /dev/mem\n"); return }

    switch opt {
        case "r":       fallthrough
        case "read":    memread(mem, addr, addr_offset)
        case "w":       fallthrough
        case "write":   memwrite(mem, addr_offset, val)
        case "sb":      fallthrough
        case "setbits": memsetbits(mem, addr_offset, val)
        case "cb":      fallthrough
        case "clrbits": memclrbits(mem, addr_offset, val)
    }

    err = syscall.Munmap(mem)
    if err != nil { fmt.Printf("Munmap failed\n") }
}

/*
#230400baud - 40000000/230400 = 173.6
./devmem write 0xd24 3          #UART1_HIGHSPEED = 3
./devmem setbits 0xd0c 0x80     #UART1_LCR.DLAB = 1
./devmem write 0xd00 1          #UART1_DLL (DLM:DLL = 1)
./devmem write 0xd04 0          #UART1_DLM
./devmem clrbits 0xd0c 0x80     #UART1_LCR.DLAB = 0
./devmem write 0xd28 174        #UART1_SAMPLE_COUNT = 174
./devmem write 0xd2c 87         #UART1_SAMPLE_POINT = 87

#460800baud = 40000000/460800 = 86.8
./devmem write 0xd24 3          #UART1_HIGHSPEED = 3
./devmem setbits 0xd0c 0x80     #UART1_LCR.DLAB = 1
./devmem write 0xd00 1          #UART1_DLL (DLM:DLL = 1)
./devmem write 0xd04 0          #UART1_DLM
./devmem clrbits 0xd0c 0x80     #UART1_LCR.DLAB = 1
./devmem write 0xd28 87         #UART1_SAMPLE_COUNT = 87
./devmem write 0xd2c 43         #UART1_SAMPLE_POINT = 43

#600000baud = 40000000/600000 = 66.6
./devmem write 0xd24 3          #UART1_HIGHSPEED = 3
./devmem setbits 0xd0c 0x80     #UART1_LCR.DLAB = 1
./devmem write 0xd00 1          #UART1_DLL (DLM:DLL = 1)
./devmem write 0xd04 0          #UART1_DLM
./devmem clrbits 0xd0c 0x80     #UART1_LCR.DLAB = 1
./devmem write 0xd28 67         #UART1_SAMPLE_COUNT = 67
./devmem write 0xd2c 33         #UART1_SAMPLE_POINT = 33

TODO: be able to run multiple commands
./devman w 0xd34 3 sb 0xd0c 0x80 w 0xd00 1 w 0xd04 0 cb 0xd0c 0x80 w 0xd28 67 w 0xd2c 33
*/

