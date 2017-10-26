#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <byteswap.h>
#include <time.h>
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

#include "toscaMap.h"
#include "toscaDma.h"

#include <iocsh.h>
#include <epicsExport.h>

#define TOSCA_EXTERN_DEBUG
#define TOSCA_DEBUG_NAME toscaDma
#include "toscaDebug.h"

static const iocshFuncDef mallocDef =
    { "malloc", 2, (const iocshArg *[]) {
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "alignment", iocshArgString },
}};

static void mallocFunc(const iocshArgBuf *args)
{
    void *p;
    char b[20];
    if (args[1].sval)
        p = memalign(toscaStrToSize(args[0].sval), toscaStrToSize(args[1].sval));
    else
        p = valloc(toscaStrToSize(args[0].sval));
    sprintf(b, "%p", p);
    setenv("BUFFER", b, 1);
    printf ("BUFFER = %s\n", b);
}

static const iocshFuncDef memfillDef =
    { "memfill", 5, (const iocshArg *[]) {
    &(iocshArg) { "address", iocshArgString },
    &(iocshArg) { "pattern", iocshArgInt },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "width", iocshArgInt },
    &(iocshArg) { "increment", iocshArgInt },
}};

static jmp_buf memfillFail;
void memfillSigAction(int sig __attribute__((unused)), siginfo_t *info, void *ctx __attribute__((unused)))
{
    fprintf(stdout, "\nInvalid address %p.\n", info->si_addr);
    longjmp(memfillFail, 1);
}

static void memfillFunc(const iocshArgBuf *args)
{
    toscaMapAddr_t addr;
    uint32_t pattern;
    size_t size;
    int width;
    int increment;
    size_t i;
    volatile void* address;

    struct sigaction sa = {
        .sa_sigaction = memfillSigAction,
        .sa_flags = SA_SIGINFO | SA_NODEFER, /* Do not block signal */
    }, oldsa;
    sigaction(SIGSEGV, &sa, &oldsa);
    if (setjmp(memfillFail) != 0)
    {
        sigaction(SIGSEGV, &oldsa, NULL);
        return;
    }

    if (!args[0].sval)
    {
        iocshCmd("help memfill");
        return;
    }
    addr = toscaStrToAddr(args[0].sval, NULL);
    pattern = args[1].ival;
    size = toscaStrToSize(args[2].sval);
    width = args[3].ival;
    increment = args[4].ival;
    
    if (addr.addrspace)
        address = toscaMap(addr.addrspace, addr.address, size, 0);
    else
        address = (volatile void*)(size_t)addr.address;
    if (!address)
    {
        error("cannot map address %s", args[0].sval);
        return;
    }

    switch (width)
    {
        case 0:
        case 1:
            for (i = 0; i < size; i++)
            {
                ((uint8_t*)address)[i] = pattern;
                pattern += increment;
            }
            break;
        case 2:
            size >>= 1;
            for (i = 0; i < size; i++)
            {
                ((uint16_t*)address)[i] = pattern;
                pattern += increment;
            }
            break;
        case 4:
            size >>= 2;
            for (i = 0; i < size; i++)
            {
                ((uint32_t*)address)[i] = pattern;
                pattern += increment;
            }
            break;
        default:
            error("Illegal width %d: must be 1, 2, or 4", width);
    }

    sigaction(SIGSEGV, &oldsa, NULL);
}

static const iocshFuncDef memcopyDef =
    { "memcopy", 4, (const iocshArg *[]) {
    &(iocshArg) { "[addrspace:]source", iocshArgString },
    &(iocshArg) { "[addrspace:]dest", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "width", iocshArgInt },
}};

void memcopyFunc(const iocshArgBuf *args)
{
    struct timespec start, finished;
    volatile void* sourceptr;
    volatile void* destptr;
    toscaMapAddr_t addr;
    size_t size, i;
    int width;

    if (!args[0].sval || !args[1].sval || !args[2].sval)
    {
        iocshCmd("help memcopy");
        return;
    }
    size = toscaStrToSize(args[2].sval);

    addr = toscaStrToAddr(args[0].sval, NULL);
    if (addr.addrspace)
        sourceptr = toscaMap(addr.addrspace, addr.address, size, 0);
    else
        sourceptr = (volatile void*)(size_t)addr.address;
    if (!sourceptr)
    {
        error("cannot map source address %s", args[0].sval);
        return;
    }

    addr = toscaStrToAddr(args[1].sval, NULL);
    if (addr.addrspace)
        destptr = toscaMap(addr.addrspace, addr.address, size, 0);
    else
        destptr = (volatile void*)toscaStrToSize(args[1].sval);
    if (!destptr)
    {
        error("cannot map dest address %s", args[1].sval);
        return;
    }

    width = args[3].ival;

    if (toscaDmaDebug)
        clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    switch (width)
    {
        case 0:
            memcpy((void*)destptr, (void*)sourceptr, size);
            break;
        case 1:
        case -1:
            for (i = 0; i < size; i++)
            {
                ((uint8_t*)destptr)[i] = ((uint8_t*)sourceptr)[i];
            }
            break;
        case 2:
            for (i = 0; i < size/2; i++)
            {
                ((uint16_t*)destptr)[i] = ((uint16_t*)sourceptr)[i];
            }
            break;
        case 4:
            for (i = 0; i < size/4; i++)
            {
                ((uint32_t*)destptr)[i] = ((uint32_t*)sourceptr)[i];
            }
            break;
        case 8:
            for (i = 0; i < size/8; i++)
            {
                ((uint64_t*)destptr)[i] = ((uint64_t*)sourceptr)[i];
            }
            break;
        case -2:
            for (i = 0; i < size/2; i++)
            {
                ((uint16_t*)destptr)[i] = bswap_16(((uint16_t*)sourceptr)[i]);
            }
            break;
        case -4:
            for (i = 0; i < size/4; i++)
            {
                ((uint32_t*)destptr)[i] = bswap_32(((uint32_t*)sourceptr)[i]);
            }
            break;
        case -8:
            for (i = 0; i < size/8; i++)
            {
                ((uint64_t*)destptr)[i] = bswap_64(((uint64_t*)sourceptr)[i]);
            }
            break;
        default:
            error("Illegal width %d: must be 1, 2, 4, 8, -2, -4, -8", width);
            return;
    }
    if (toscaDmaDebug)
    {
        double sec;
        clock_gettime(CLOCK_MONOTONIC_RAW, &finished);
        finished.tv_sec  -= start.tv_sec;
        if ((finished.tv_nsec -= start.tv_nsec) < 0)
        {
            finished.tv_nsec += 1000000000;
            finished.tv_sec--;
        }
        sec = finished.tv_sec + finished.tv_nsec * 1e-9;
        debug("%zu %sB / %.3f msec (%.1f MiB/s = %.1f MB/s)",
            size >= 0x00100000 ? (size >> 20) : size >= 0x00000400 ? (size >> 10) : size,
            size >= 0x00100000 ? "Mi" : size >= 0x00000400 ? "Ki" : "",
            sec * 1000, size/sec/0x00100000, size/sec/1000000);
    }
    
}

static const iocshFuncDef memcompDef =
    { "memcomp", 4, (const iocshArg *[]) {
    &(iocshArg) { "[addrspace:]source", iocshArgString },
    &(iocshArg) { "[addrspace:]dest", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "width", iocshArgInt },
}};

static void memcompFunc(const iocshArgBuf *args)
{
    volatile uint32_t* sourceptr;
    volatile uint32_t* destptr;
    toscaMapAddr_t addr;
    size_t size, i;
    int width;

    if (!args[0].sval || !args[1].sval || !args[2].sval)
    {
        iocshCmd("help memcomp");
        return;
    }
    size = toscaStrToSize(args[2].sval);

    addr = toscaStrToAddr(args[0].sval, NULL);
    if (addr.addrspace)
        sourceptr = toscaMap(addr.addrspace, addr.address, size, 0);
    else
        sourceptr = (volatile void*)(size_t)addr.address;
    if (!sourceptr)
    {
        error("cannot map source address %s", args[0].sval);
        return;
    }

    addr = toscaStrToAddr(args[1].sval, NULL);
    if (addr.addrspace)
        destptr = toscaMap(addr.addrspace, addr.address, size, 0);
    else
        destptr = (volatile void*)toscaStrToSize(args[1].sval);
    if (!destptr)
    {
        error("cannot map dest address %s", args[1].sval);
        return;
    }

    width = args[3].ival;

    switch (width)
    {
        case 0:
        case 1:
        case -1:
            for (i = 0; i < size; i++)
            {
                if (((uint8_t*)destptr)[i] != ((uint8_t*)sourceptr)[i]) break;
            }
            break;
        case 2:
            for (i = 0; i < size; i+=2)
            {
                if (((uint16_t*)destptr)[i/2] != ((uint16_t*)sourceptr)[i/2]) break;
            }
            break;
        case 4:
            for (i = 0; i < size; i+=4)
            {
                if (((uint32_t*)destptr)[i/4] != ((uint32_t*)sourceptr)[i/4]) break;
            }
            break;
        case 8:
            for (i = 0; i < size; i+=8)
            {
                if (((uint64_t*)destptr)[i/8] != ((uint64_t*)sourceptr)[i/8]) break;
            }
            break;
        case -2:
            for (i = 0; i < size; i+=2)
            {
                if (((uint16_t*)destptr)[i/2] != bswap_16(((uint16_t*)sourceptr)[i/2])) break;
            }
            break;
        case -4:
            for (i = 0; i < size; i+=4)
            {
                if (((uint32_t*)destptr)[i/4] != bswap_32(((uint32_t*)sourceptr)[i/4])) break;
            }
            break;
        case -8:
            for (i = 0; i < size; i+=8)
            {
                if (((uint64_t*)destptr)[i/8] != bswap_64(((uint64_t*)sourceptr)[i/8])) break;
            }
            break;
        default:
            error("Illegal width %d: must be 1, 2, 4, 8, -2, -4, -8", width);
            return;
    }
    if (i < size)
        printf("Mismatch at offset 0x%zx\n", i);
    else
        printf("OK\n");
}

static void toscaUtilsRegistrar(void)
{
    iocshRegister(&mallocDef, mallocFunc);
    iocshRegister(&memfillDef, memfillFunc);
    iocshRegister(&memcopyDef, memcopyFunc);
    iocshRegister(&memcompDef, memcompFunc);
}

epicsExportRegistrar(toscaUtilsRegistrar);
