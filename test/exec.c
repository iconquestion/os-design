#include "syscall.h"

int
main() {
    SpaceId pid;

    Write("exec.c running 0\n", 17, ConsoleOutput);
    pid = Exec("../test/halt.noff");
    Write("exec.c running 1\n", 17, ConsoleOutput);
    if (pid < 0) {
        Exit(-1);
    }
    Yield();
    Exit(0);
}
