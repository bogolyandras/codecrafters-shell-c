#ifndef BOGOLYANDRAS_PLATFORM_WIN32_H
#define BOGOLYANDRAS_PLATFORM_WIN32_H

/** Windows only, shared by the files in this directory: sets errno from the calling thread's last Windows error */
void platform_set_errno_from_last_error(void);

#endif //BOGOLYANDRAS_PLATFORM_WIN32_H
