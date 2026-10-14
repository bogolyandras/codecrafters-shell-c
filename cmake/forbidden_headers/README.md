Headers that the core must not include.

`src/core` is compiled with this directory first on its include path, so `#include <unistd.h>`,
`<windows.h>` and the like find the `#error` file here instead of the real header. Only the
platform modules (`src/posix`, `src/win32`) may use them.

Only names that no standard C header includes on any supported toolchain are here. Others, such as
`<sys/stat.h>` (MinGW's `<wchar.h>` includes it), `<fcntl.h>`, `<io.h>` and `<direct.h>`, cannot be
shadowed without breaking the standard headers, so `cmake/check_platform_includes.cmake` checks
them as text instead. It also covers the module the local compiler does not build.
