#ifndef BOGOLYANDRAS_SHELL_H
#define BOGOLYANDRAS_SHELL_H

/**
 * The shell itself: start-up, the read-eval loop and clean-up. The platform's entry point
 * (main_posix.c or main_win32.c) only calls this. Returns the exit status of the process.
 */
int shell_run(void);

#endif //BOGOLYANDRAS_SHELL_H
