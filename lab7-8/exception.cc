// exception.cc
//	Entry point into the Nachos kernel from user programs.

#include "copyright.h"
#include "system.h"
#include "syscall.h"
#include "addrspace.h"
#include "synch.h"

static const int kMaxUserStringLength = 256;
static const int kMaxProcesses = 64;
static int nextPid = 1;

struct ProcessInfo
{
    int pid;
    int parentPid;
    int exitStatus;
    bool exited;
    bool joined;
    Thread *thread;
    Semaphore *exitSem;
};

static ProcessInfo *processTable[kMaxProcesses];

static void StartUserProcess(_int arg);

static void
AdvanceProgramCounter()
{
    int pc = machine->ReadRegister(PCReg);
    machine->WriteRegister(PrevPCReg, pc);
    machine->WriteRegister(PCReg, pc + 4);
    machine->WriteRegister(NextPCReg, pc + 8);
}

static char *
CopyUserString(int userAddr)
{
    char *buffer = new char[kMaxUserStringLength];
    int ch = 0;

    for (int i = 0; i < kMaxUserStringLength - 1; i++)
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

static void
WriteToConsole(int userBufferAddr, int size)
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

static int
ReadFromConsole(int userBufferAddr, int size)
{
    int count = 0;

    for (; count < size; count++)
    {
        int ch = getchar();
        if (ch == EOF)
        {
            break;
        }
        if (!machine->WriteMem(userBufferAddr + count, 1, ch & 0xff))
        {
            break;
        }
    }

    return count;
}

static ProcessInfo *
FindProcess(int pid)
{
    for (int i = 0; i < kMaxProcesses; i++)
    {
        if (processTable[i] != NULL && processTable[i]->pid == pid)
        {
            return processTable[i];
        }
    }
    return NULL;
}

static void
RemoveProcess(int pid)
{
    for (int i = 0; i < kMaxProcesses; i++)
    {
        if (processTable[i] != NULL && processTable[i]->pid == pid)
        {
            delete processTable[i]->exitSem;
            delete processTable[i];
            processTable[i] = NULL;
            return;
        }
    }
}

static int
CreateProcessEntry(Thread *thread, int parentPid)
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    int slot = -1;

    for (int i = 0; i < kMaxProcesses; i++)
    {
        if (processTable[i] == NULL)
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
    {
        (void)interrupt->SetLevel(oldLevel);
        return -1;
    }

    ProcessInfo *info = new ProcessInfo;
    info->pid = nextPid++;
    info->parentPid = parentPid;
    info->exitStatus = 0;
    info->exited = FALSE;
    info->joined = FALSE;
    info->thread = thread;
    info->exitSem = new Semaphore("process exit", 0);
    processTable[slot] = info;

    thread->processId = info->pid;
    thread->parentProcessId = parentPid;
    thread->exitStatus = 0;

    (void)interrupt->SetLevel(oldLevel);
    return info->pid;
}

static bool
HasReadyThread()
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

static void
DoExit(int status)
{
    AddrSpace *space = currentThread->space;
    ProcessInfo *info = FindProcess(currentThread->processId);

    printf("Process %d exiting with status %d\n",
           currentThread->processId, status);

    currentThread->exitStatus = status;
    if (info != NULL)
    {
        info->exitStatus = status;
        info->exited = TRUE;
        info->exitSem->V();
    }

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

static void
StartUserProcess(_int arg)
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

static int
DoExec(int userFileName)
{
    char *filename = CopyUserString(userFileName);
    OpenFile *testOpen;
    unsigned int requiredPages;

    if (filename == NULL || filename[0] == '\0')
    {
        if (filename != NULL)
        {
            delete[] filename;
        }
        return -1;
    }

    testOpen = fileSystem->Open(filename);
    if (testOpen == NULL)
    {
        delete[] filename;
        return -1;
    }

    requiredPages = AddrSpace::RequiredPages(testOpen);
    if (requiredPages > AddrSpace::NumFreeFrames())
    {
        delete testOpen;
        delete[] filename;
        return -1;
    }
    delete testOpen;

    Thread *childThread = new Thread("user process");
    int pid = CreateProcessEntry(childThread, currentThread->processId);
    if (pid < 0)
    {
        delete childThread;
        delete[] filename;
        return -1;
    }

    childThread->Fork(StartUserProcess, (_int)filename);
    return pid;
}

static int
DoJoin(int pid)
{
    ProcessInfo *info = FindProcess(pid);
    if (info == NULL || info->parentPid != currentThread->processId ||
        info->joined)
    {
        return -1;
    }

    info->joined = TRUE;
    if (!info->exited)
    {
        info->exitSem->P();
    }

    int status = info->exitStatus;
    RemoveProcess(pid);
    return status;
}

void
ExceptionHandler(ExceptionType which)
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
            machine->WriteRegister(2, DoExec(machine->ReadRegister(4)));
            AdvanceProgramCounter();
            return;
        case SC_Join:
            machine->WriteRegister(2, DoJoin(machine->ReadRegister(4)));
            AdvanceProgramCounter();
            return;
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
        case SC_Read:
            if (machine->ReadRegister(6) == ConsoleInput)
            {
                machine->WriteRegister(2,
                                       ReadFromConsole(machine->ReadRegister(4),
                                                       machine->ReadRegister(5)));
            }
            else
            {
                machine->WriteRegister(2, -1);
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
