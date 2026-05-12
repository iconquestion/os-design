#include "syscall.h"

int
main() {
    Write("exit.c running\n", 15, ConsoleOutput);
    Exit(7);
}
