// exception.cc
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"
#include "addrspace.h"

static const int kMaxUserStringLength = 256;
static int nextSpaceId = 1;

static void StartUserProcess(_int arg);

static void AdvanceProgramCounter()
{
    int pc = machine->ReadRegister(PCReg);
    machine->WriteRegister(PrevPCReg, pc);
    machine->WriteRegister(PCReg, pc + 4);
    machine->WriteRegister(NextPCReg, pc + 8);
}

static void WriteToConsole(int userBufferAddr, int size)
{
    for (int i = 0; i < size; i++)
    {
        int ch;
        if (!machine->ReadMem(userBufferAddr + i, 1, &ch))
        {
            return;
        }
        putchar(ch & 0xff);
    }
    fflush(stdout);
}

static char *CopyUserString(int userAddr)
{
    char *buffer = new char[kMaxUserStringLength];
    int ch = 0;
    int i;

    for (i = 0; i < kMaxUserStringLength - 1; i++)
    {
        if (!machine->ReadMem(userAddr + i, 1, &ch))
        {
            delete[] buffer;
            return NULL;
        }
        buffer[i] = (char)(ch & 0xff);
        if (buffer[i] == '\0')
        {
            return buffer;
        }
    }

    buffer[kMaxUserStringLength - 1] = '\0';
    return buffer;
}

static bool HasReadyThread()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    Thread *nextThread = scheduler->FindNextToRun();
    bool hasReadyThread = (nextThread != NULL);

    if (nextThread != NULL)
    {
        scheduler->ReadyToRun(nextThread);
    }

    (void)interrupt->SetLevel(oldLevel);
    return hasReadyThread;
}

static void DoExit(int status)
{
    AddrSpace *space = currentThread->space;

    printf("Process %s exiting with status %d\n",
           currentThread->getName(), status);

    currentThread->space = NULL;
    if (space != NULL)
    {
        delete space;
    }

    if (HasReadyThread())
    {
        currentThread->Finish();
    }
    else
    {
        interrupt->Halt();
    }
}

static void StartUserProcess(_int arg)
{
    char *filename = (char *)arg;
    OpenFile *executable = fileSystem->Open(filename);

    if (executable == NULL)
    {
        printf("Unable to open file %s\n", filename);
        delete[] filename;
        DoExit(-1);
        return;
    }

    currentThread->space = new AddrSpace(executable);
    currentThread->space->Print();

    delete executable;
    delete[] filename;

    currentThread->space->InitRegisters();
    currentThread->space->RestoreState();
    machine->Run();

    ASSERT(FALSE);
}

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2.
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions
//	are in machine.h.
//----------------------------------------------------------------------

void ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);

    if (which == SyscallException)
    {
        switch (type)
        {
        case SC_Halt:
            DEBUG('a', "Shutdown, initiated by user program.\n");
            interrupt->Halt();
            return;
        case SC_Exit:
            DoExit(machine->ReadRegister(4));
            return;
        case SC_Exec:
        {
            char *filename = CopyUserString(machine->ReadRegister(4));
            Thread *childThread;

            if (filename == NULL || filename[0] == '\0')
            {
                machine->WriteRegister(2, -1);
                if (filename != NULL)
                {
                    delete[] filename;
                }
                AdvanceProgramCounter();
                return;
            }

            childThread = new Thread("user process");
            childThread->Fork(StartUserProcess, (_int)filename);

            machine->WriteRegister(2, nextSpaceId++);
            AdvanceProgramCounter();
            return;
        }
        case SC_Write:
            if (machine->ReadRegister(6) == ConsoleOutput)
            {
                WriteToConsole(machine->ReadRegister(4),
                               machine->ReadRegister(5));
            }
            else
            {
                printf("Unsupported Write target %d\n",
                       machine->ReadRegister(6));
            }
            AdvanceProgramCounter();
            return;
        case SC_Yield:
            AdvanceProgramCounter();
            currentThread->Yield();
            return;
        default:
            break;
        }
    }

    printf("Unexpected user mode exception %d %d\n", which, type);
    ASSERT(FALSE);
}
