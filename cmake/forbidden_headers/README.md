Headers that the core must not include.

`src/core` is compiled with this directory first on its include path, so `#include <unistd.h>`,
`<windows.h>` and the like find the `#error` file here instead of the real header. Only the
platform modules (`src/posix`, `src/win32`) may use them. See `cmake/check_platform_includes.cmake`
for the checks that no compiler can do on a single system.
