//=============================================================================
// devmem
// read/write to hardware registers on Onion Omega2
//=============================================================================

#include <stdio.h>
#include <sys/mman.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h> //close
#include <stdlib.h>
#include <stdbool.h>
#include <libgen.h> //basename

//=============================================================================
// defines
//=============================================================================
#define MAX_ARGS 255                    //limit # of args to program
#define BASE_ADDR 0x10000000            //Omega2 base register address

//=============================================================================
// command buffer struct and global buffer
//=============================================================================
typedef struct {
    char cmd;
    uint32_t addr;
    uint32_t val;
} cmd_buffer_t;
cmd_buffer_t g_cmdbuffer[MAX_ARGS+1];   //store all validated commands/values

//=============================================================================
// show usage
//=============================================================================
static void usage(const char* n){
    printf(
        "\n"
        " %s r <addr>            :read address, return decimal\n"
        " %s h <addr>            :read address, return hex\n"
        " %s b <addr>            :read address, return binary\n"
        " %s v <addr>            :read address, verbose output\n"
        " %s w <addr> <val>      :write address value\n"
        " %s s <addr> <bitmask>  :set bitmask bit(s) at address\n"
        " %s c <addr> <bitmask>  :clear bitmask bit(s) at address\n"
        "\n"
        "    <addr> absolute address or offset address from 0x10000000\n"
        "       address will be automatically word aligned\n"
        "    <val> 32bit value\n"
        "    <bitmask> 32bit bitmask value (1=set or clr specified bit)\n"
        "\n"
        "    numbers can be in any format-\n"
        "       decimal, hex (0x_), binary (0b_), octal (0_)\n"
        "    can provide more than one command at a time, up to a limit\n"
        "       of 255 arguments\n"
        "\n",
        n,n,n,n,n,n,n
    );
    exit(EXIT_FAILURE);
}

//=============================================================================
// print error message, then exit
//=============================================================================
static void error_exit(const char* msg)
{
    if(msg) fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

//=============================================================================
// value conversion argument error
//=============================================================================
static void arg_error_exit(int argn, const char* str)
{
    fprintf(stderr, "argument# %d -> %s\n", argn, str);
    error_exit("invalid command or value");
}

//=============================================================================
// map physical address to virtual memory, 1 page will be mapped
// error/exit on any failures
// return pointer to virtual address where word is located
//=============================================================================
static uint32_t* regmap(uint32_t addr)
{
    static uint32_t m_page;     //current physical page mapped
    static void* m_vmem;        //current mmap pointer
    static uint32_t m_pagesize; //system page size

    //get system page size (most likely 4096), get/store once
    if(m_pagesize == 0) m_pagesize = sysconf(_SC_PAGE_SIZE);
    //if same page already mapped, no need to map again
    if(m_vmem && (addr / m_pagesize * m_pagesize) == m_page) goto ret;
    //if previously mapped and unmap fails (!0), error
    if(m_vmem && munmap(m_vmem, m_pagesize)){
        error_exit("failed to unmap register memory");
    }

    int fd = open("/dev/mem", O_RDWR);
    if(fd < 0) error_exit("failed to open /dev/mem");
    m_page = addr / m_pagesize * m_pagesize;
	m_vmem = mmap(
        NULL,                   //mmap determines virtual address
        m_pagesize,             //length (just get a whole page)
        PROT_READ | PROT_WRITE, //match file open
        MAP_FILE | MAP_SHARED,
        fd,                     //file
        m_page                  //offset into fd (page aligned)
    );
    close(fd);                  //fd no longer needed
    if(m_vmem == MAP_FAILED) error_exit("failed to mmap register memory");

    ret:
	return &((uint32_t*)m_vmem)[(addr-m_page)/4];
}

//=============================================================================
// convert command line value argument to unsigned integer
// exit on any failure
//=============================================================================
int strto_u32(const char* str, int argn)
{
    if(*str == '-') arg_error_exit(argn, str); //disallow negative numbers
    char *endptr = NULL;
    const char *s = str;
    int base = 0;                   //auto for decimal, hex, octal
    if(s[0] == '0' && s[1] == 'b'){ //check for binary
        base = 2;                   //is binary
        s += 2;                     //skip over "0b"
    }
    uint32_t val = strtoul(s, &endptr, base);
    if(*endptr) arg_error_exit(argn, str); //failed if *endptr not 0
    return val;                     //conversion ok
}

//=============================================================================
// validate everything on command line before doing anything
// (either all good, or all fails, no partital writes)
// store everything in g_cmdbuffer so main loop only has to go through already
//   validated/converted buffer
// g_cmdbuffer[] = { cmd, address, value, ..., 0, 0, 0 }
//=============================================================================
bool validate_command_line(int argc, char** argv)
{
    cmd_buffer_t* cb = &g_cmdbuffer[0];
    for(int i = 0; i < argc; cb++){
        cb->cmd = argv[i][0];
        //only single letter command allowed
        if(argv[i++][1]) arg_error_exit(i, argv[i-1]);
        if(i >= argc) error_exit("incomplete command");
        cb->addr = strto_u32(argv[i], i+1);
        //get addr into 0x1xxxxxxx and word aligned
        cb->addr &= (BASE_ADDR-1);  // 0x0.......
        cb->addr |= BASE_ADDR;      // 0x1.......
        cb->addr &= ~3;             // 0x.......n (n = 0b..00)
        i++;
        switch(cb->cmd){
            case 'r': case 'h': case 'b': case 'v':
                break; //already got address, nothing to do
            case 'w': case 's': case 'c':
                if(i >= argc) error_exit("incomplete command");
                cb->val = strto_u32(argv[i], i+1);
                i++;
                break;
            default:
                arg_error_exit(i, argv[i-1]);
                break;
        }
    }
}

//=============================================================================
// read register 'v' verbose, 'h' hex, 'r' decimal, 'b' binary
//=============================================================================
void regread(char opt, uint32_t addr, uint32_t val)
{
    uint8_t b0 = val;
    uint8_t b1 = val>>8;
    uint8_t b2 = val>>16;
    uint8_t b3 = val>>24;

    if(opt == 'v'){
        printf(
            "\n"
            "addr: [0x%08X] val: [0x%08X]\n"
            "\n"
            "bit 3 3 2 2 2 2 2 2  2 2 2 1 1 1 1 1  1 1 1 1 1 1\n"
            "pos 1 0 9 8 7 6 5 4  3 2 1 0 9 8 7 6  5 4 3 2 1 0 9 8  7 6 5 4 3 2 1 0\n"
            "    ...............  ...............  ...............  ...............\n"
            "bin ",
            addr, val);
        for(int i = 0; i < 32; i++, val<<=1){
            printf("%s", val & (1<<31) ? "1 " : "0 " );
            if((i & 7) == 7) printf(" ");
        }
        printf(
            "\n"
            "hex %11s0x%02X  %11s0x%02X  %11s0x%02X  %11s0x%02X\n"
            "dec %15d  %15d  %15d  %15d\n\n",
            " ", b3, " ", b2, " ", b1, " ", b0, b3, b2, b1, b0);
    } else if(opt == 'h'){
        printf("0x%08X\n", val);
    } else if(opt == 'b'){
        printf("0b ");
        for(int i = 0; i < 32; i++, val<<=1){
            printf("%s", val & (1<<31) ? "1" : "0");
            if((i & 7) == 7) printf(" ");
        }
        printf("\n");
    } else {
        printf("%d\n", val);
    }

}

//=============================================================================
// MAIN
//=============================================================================
int main(int argc, char** argv)
{
    //limit args to a reasonable number
    if(argc > MAX_ARGS) error_exit("argument count exceeded");

    //check argc, if no args show usage
    //let basename modify argv[0]- is ok
    if(argc < 2) usage(basename(argv[0]));

    //validate (starting at first arg)
    validate_command_line(argc-1, &argv[1]);

    //iterate over validated command line buffer
    for(cmd_buffer_t* cb = &g_cmdbuffer[0]; cb->cmd; cb++){
        uint32_t* p32 = regmap(cb->addr);
        switch(cb->cmd){
            case 'r':
            case 'h':
            case 'v':
            case 'b':
                regread( cb->cmd, cb->addr, *p32 );
                break;
            case 'w': *p32 = cb->val;
                break;
            case 's': *p32 |= cb->val;
                break;
            case 'c': *p32 &= ~cb->val;
                break;
        }
    }

    exit(EXIT_SUCCESS);
}
