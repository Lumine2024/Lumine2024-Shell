#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lumine_sh {

struct PipeEnd {
#ifdef _WIN32
    HANDLE handle = nullptr;
#else
    int fd = -1;
#endif
};

}
