#include <stdio.h>
#include <sys/mman.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h> //close
#include <stdlib.h>
#include <stdbool.h>
#include <libgen.h> //basename

//command buffer struct
typedef struct {
    char cmd; 
    uint32_t addr;
    uint32_t val;
} cmd_buffer_t;
//GLOBAL--
#define MAX_ARGS 255                    //limit #args to program
#define BASE_ADDR 0x10000000            //Omega2 base register address
uint32_t g_pagesize;                    //page size (will get once, store here)
cmd_buffer_t g_cmdbuffer[MAX_ARGS+1];   //store all commands/values when validated
//--GLOBAL

//show usage
static void usage(const char* n){
    printf(
        "\n"
        " %s r <addr>            :read address, return decimal\n"
        " %s h <addr>            :read address, return hex\n"
        " %s b <addr>            :read address, return binary\n"
        " %s w <addr> <val>      :write address value\n"
        " %s s <addr> <bitmask>  :set bit(s) address bitmask\n"
        " %s c <addr> <bitmask>  :clear bit(s) address bitmask\n"
        "\n"
        "    <addr> absolute address or offset address from 0x10000000\n"
        "    <val> 32bit value\n"
        "    <bitmask> 32bit bitmask value (1=set or clr specified bit)\n"
        "    numbers can be in any format- decimal, hex (0x_), binary (0b_), octal (0_)\n"
        "\n",
        n,n,n,n,n,n
    );
    exit(EXIT_FAILURE);
} 

//will print error message, then exit
static void error_exit(const char* msg)
{
    if(msg) fprintf(stderr, "%s\n", msg); 
    exit(EXIT_FAILURE);
} 

//value conversion argument error
static void arg_error_exit(int argn, const char* str)
{ 
    fprintf(stderr, "argument# %d -> %s\n", argn, str);
    error_exit("invalid command or value"); 
}

//unmap previously mmapped memory, error/exit if failes to unmap
static void regunmap(void* vaddr)
{
    //if previously mapped and unmap fails (!0)
    if(vaddr && munmap(vaddr, g_pagesize)){
        error_exit("failed to unmap register memory");
    }
}

//map physical address to virtual memory, 1 page will be mapped
//any failures error/exit
//return pointer to virtual address where word is located
static uint32_t* regmap(uint32_t addr)
{
    static uint32_t m_page;     //current physical page mapped
    static void* m_vmem;        //current mmap pointer
    
    //if same page already mapped, no need to map again
    //else unmap and get new mmap
    if(m_vmem){        
        if((addr / g_pagesize * g_pagesize) == m_page){
            return &((uint32_t*)m_vmem)[(addr-m_page)/4];
        }
        regunmap(m_vmem);
    }
    int fd = open("/dev/mem", O_RDWR);
    if(fd < 0) error_exit("failed to open /dev/mem");
    m_page = addr / g_pagesize * g_pagesize;
	m_vmem = mmap(
        NULL,       //mmap determines virtual address
        g_pagesize, //length (just get a whole page)
        PROT_READ | PROT_WRITE, //match file open
        MAP_FILE | MAP_SHARED,
        fd,         //file
        m_page      //offset into fd (page aligned)
    );
    close(fd);      //no longer needed
    if(m_vmem == MAP_FAILED) error_exit("failed to mmap register memory");
	return &((uint32_t*)m_vmem)[(addr-m_page)/4];
}

int strto_u32(const char* str, int argn)
{   
    //disallow negative numbers
    if(*str == '-') arg_error_exit(argn, str);
    char *endptr = NULL;
    const char *s = str;
    int base = 0; //auto for decimal, hex, octal
    //check for binary
    if(s[0] == '0' && s[1] == 'b'){
        base = 2;
        s += 2; //skip "0b"
    }
    uint32_t val = strtoul(s, &endptr, base);
    if(*endptr == 0) return val;
    arg_error_exit(argn, str);  
}

//validate everything on command line before doing anything
//(either all good, or all fails, no partital writes)
//store everything in g_cmdbuffer so main loop only has to 
//go through already validated/converted buffer
//g_cmdbuffer[] = { cmd, uint32, uint32, ..., 0, 0, 0 }
bool validate_command_line(int argc, char** argv)
{
    for(int i = 0, idx = 0; i < argc; idx++){
        char cmd = argv[i++][0];
        uint32_t val = 0;
        uint32_t addr = 0;
        switch(cmd){
            case 'r':
            case 'h':
            case 'b':
                if(i >= argc) error_exit("incomplete command");
                addr = strto_u32(argv[i], i+1);
                i++;
                break;
            case 'w':
            case 's':
            case 'c':
                if(i+1 >= argc) error_exit("incomplete command");
                addr = strto_u32(argv[i], i+1);
                val = strto_u32(argv[i+1], i+2);
                i += 2;
                break;
            default:
                arg_error_exit(i, argv[i-1]);
                break;
        }
        g_cmdbuffer[idx].cmd = cmd;
        //get addr into 0x1xxxxxxx and word aligned
        g_cmdbuffer[idx].addr = addr & (BASE_ADDR-1) | BASE_ADDR & ~3;
        g_cmdbuffer[idx].val = val;        
    }
}

int main(int argc, char** argv)
{
    //limit args to a reasonable number
    if(argc > MAX_ARGS) error_exit("argument count exceeded");

    //get system page size (most likely 4096), get once
    g_pagesize = sysconf(_SC_PAGE_SIZE);
   
    //check argc, need at least 3 (progname r addr)
    //let basename modify argv[0]- is ok
    if(argc < 3) usage(basename(argv[0]));
    
    //validate (starting at first arg)
    validate_command_line(argc-1, &argv[1]);
       
    //iterate over validated command line buffer
    for(int i = 0; g_cmdbuffer[i].cmd; i++){
        cmd_buffer_t cb = g_cmdbuffer[i];
        switch(cb.cmd){
            case 'r':             
                printf( "%d\n", *(regmap(cb.addr)) );
                break;
            case 'h':            
                printf( "0x%08X\n", *(regmap(cb.addr)) );
                break;                
            case 'b':
                cb.val = *(regmap(cb.addr));
                printf("0b ");
                for(int k = 0; k < 32; k++, cb.val<<=1){
                    printf("%s", cb.val & 0x80000000 ? "1" : "0");
                    if((k & 7) == 7) printf(" ");                    
                }
                printf("\n");
                break;
            case 'w':
                *(regmap(cb.addr)) = cb.val;
                break;
            case 's':
                *(regmap(cb.addr)) |= cb.val;
                break;
            case 'c':
                *(regmap(cb.addr)) &= ~cb.val;
                break;
        }
    }
       
    exit(EXIT_SUCCESS);    
}