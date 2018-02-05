/*
 * procstat.c
 *
 *		Module to report process statistics.
 *		SHOULD STUDY RUSAGE IF IT IS NOW COMPLETELY IMPLEMENTD.
 *
 *		http://stackoverflow.com/questions/63166/how-to-determine-cpu-and-memory-consumption-from-inside-a-process
 *
 * On Linux the choice that seemed obvious at first was to use the POSIX APIs
 * like getrusage() etc. I spent some time trying to get this to work,
 * but never got meaningful values. When I finally checked the kernel sources
 * themselves, I found out that apparently these APIs are not yet completely
 * implemented as of Linux kernel 2.6!?
 *
 * In the end I got all values via a combination of reading the
 * pseudo-filesystem /proc and kernel calls.
 */

#include "sys/types.h"
#include "sys/sysinfo.h"


/*
 ' Total Virtual Memory:
 */
long long total_virtual_memory()
{
    struct sysinfo memInfo;

    sysinfo (&memInfo);
    long long totalVirtualMem = memInfo.totalram;
    //Add other values in next statement to avoid int overflow on right hand side...
    totalVirtualMem += memInfo.totalswap;
    totalVirtualMem *= memInfo.mem_unit;

    return totalVirtualMem;
}

/*
 * Virtual Memory currently used
 * (Same code as in "Total Virtual Memory" and then..)
 */
long long total_virtual_memory_used()
{
    struct sysinfo memInfo;

    sysinfo (&memInfo);
    long long virtualMemUsed = memInfo.totalram - memInfo.freeram;
    //Add other values in next statement to avoid int overflow on right hand side...
    virtualMemUsed += memInfo.totalswap - memInfo.freeswap;
    virtualMemUsed *= memInfo.mem_unit;

    return virtualMemUsed;
}

/*
 * Total Physical Memory (RAM):
 * (Same code as in "Total Virtual Memory" and then..)
 */
long long total_physical_memory()
{
    struct sysinfo memInfo;

    sysinfo (&memInfo);
    long long totalPhysMem = memInfo.totalram;
    //Multiply in next statement to avoid int overflow on right hand side...
    totalPhysMem *= memInfo.mem_unit;

    return totalPhysMem;
}

/*
 * Physical Memory currently used:
 * (Same code as in "Total Virtual Memory" and then..)
 */
long long total_physical_memory_used()
{
    struct sysinfo memInfo;

    sysinfo (&memInfo);
    long long physMemUsed = memInfo.totalram - memInfo.freeram;
    //Multiply in next statement to avoid int overflow on right hand side...
    physMemUsed *= memInfo.mem_unit;

    return totalPhysMem;
}

/*
 * Physical Memory currently used by current process:
 * Change getValue() in "Virtual Memory currently used by current process" as follows:
 * Note: this value is in KB!
 */
int getValue()
{
    FILE* file = fopen("/proc/self/status", "r");
    int result = -1;
    char line[128];

    while (fgets(line, 128, file) != NULL){
        if (strncmp(line, "VmRSS:", 6) == 0){
            result = parseLine(line);
            break;
        }
    }
    fclose(file);
    return result;
}

/*
 * Virtual Memory currently used by current process:
 */
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

int parseLine(char* line){
    // This assumes that a digit will be found and the line ends in " Kb".
    int i = strlen(line);
    const char* p = line;
    while (*p <'0' || *p > '9') p++;
    line[i-3] = '\0';
    i = atoi(p);
    return i;
}

int getValue(){ //Note: this value is in KB!
    FILE* file = fopen("/proc/self/status", "r");
    int result = -1;
    char line[128];

    while (fgets(line, 128, file) != NULL){
        if (strncmp(line, "VmSize:", 7) == 0){
            result = parseLine(line);
            break;
        }
    }
    fclose(file);
    return result;
}

