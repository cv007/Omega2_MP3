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
// command buffer struct definition
//=============================================================================
typedef struct {
    char cmd;
    uint32_t addr;
    uint32_t val;
} cmd_buffer_t;

//=============================================================================
// defines and GLOBAL vars
//=============================================================================
#define MAX_ARGS 255                    //limit # of args to program
#define BASE_ADDR 0x10000000            //Omega2 base register address
uint32_t g_pagesize;                    //page size (will get once, store here)
cmd_buffer_t g_cmdbuffer[MAX_ARGS+1];   //store all commands/values when validated

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
// unmap previously mmapped memory, error/exit if fails to unmap
//=============================================================================
static void regunmap(void* vaddr)
{
    //if previously mapped and unmap fails (!0)
    if(vaddr && munmap(vaddr, g_pagesize)){
        error_exit("failed to unmap register memory");
    }
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

    //if same page already mapped, no need to map again
    if(m_vmem && (addr / g_pagesize * g_pagesize) == m_page) goto ret;

    //unmap and get new map
    regunmap(m_vmem);
    int fd = open("/dev/mem", O_RDWR);
    if(fd < 0) error_exit("failed to open /dev/mem");
    m_page = addr / g_pagesize * g_pagesize;
	m_vmem = mmap(
        NULL,                   //mmap determines virtual address
        g_pagesize,             //length (just get a whole page)
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
    if(*endptr == 0) return val;    //conversion ok
    arg_error_exit(argn, str);      //failed
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
    //argn is always i+1, argn is 1 based for arg errors
    for(int i = 0, idx = 0, argn = 1; i < argc; idx++){
        g_cmdbuffer[idx].cmd = argv[i][0];
        //only single letter command allowed
        if(argv[i][1]) arg_error_exit(argn, argv[i]);
        i++; argn++;
        switch(g_cmdbuffer[idx].cmd){
            case 'r': case 'h': case 'b': case 'v':
                if(i >= argc) error_exit("incomplete command");
                g_cmdbuffer[idx].addr = strto_u32(argv[i++], argn++);
                break;
            case 'w': case 's': case 'c':
                if(argn >= argc) error_exit("incomplete command");
                g_cmdbuffer[idx].addr = strto_u32(argv[i++], argn++);
                g_cmdbuffer[idx].val = strto_u32(argv[i++], argn++);
                break;
            default:
                arg_error_exit(i, argv[i-1]);
                break;
        }
        //get addr into 0x1xxxxxxx and word aligned
        g_cmdbuffer[idx].addr &= (BASE_ADDR-1);
        g_cmdbuffer[idx].addr |= BASE_ADDR;
        g_cmdbuffer[idx].addr &= ~3;
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

    //get system page size (most likely 4096), get/store once
    g_pagesize = sysconf(_SC_PAGE_SIZE);

    //check argc, if no args show usage
    //let basename modify argv[0]- is ok
    if(argc < 2) usage(basename(argv[0]));

    //validate (starting at first arg)
    validate_command_line(argc-1, &argv[1]);

    //iterate over validated command line buffer
    for(int i = 0; g_cmdbuffer[i].cmd; i++){
        cmd_buffer_t cb = g_cmdbuffer[i];
        switch(cb.cmd){
            case 'r':
            case 'h':
            case 'v':
            case 'b':
                regread( cb.cmd, cb.addr, *(regmap(cb.addr)) );
                break;
            case 'w': *(regmap(cb.addr)) = cb.val;   break;
            case 's': *(regmap(cb.addr)) |= cb.val;  break;
            case 'c': *(regmap(cb.addr)) &= ~cb.val; break;
        }
    }

    exit(EXIT_SUCCESS);
}
