#include "syscall.h"

int
main()
{
    SpaceId pid;
    int status;

    Write("join.c start\n", 13, ConsoleOutput);

    pid = Exec("../test/exit.noff");
    if (pid < 0)
    {
        Write("join exec failed\n", 17, ConsoleOutput);
        Exit(-1);
    }

    status = Join(pid);
    if (status != 7)
    {
        Write("join returned wrong status\n", 27, ConsoleOutput);
        Exit(-1);
    }

    if (Join(pid) != -1)
    {
        Write("join should fail twice\n", 23, ConsoleOutput);
        Exit(-1);
    }

    Write("join ok\n", 8, ConsoleOutput);
    Exit(0);
}
