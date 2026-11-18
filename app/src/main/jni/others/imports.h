#pragma once
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <math.h>
#include <thread>
#include <sys/mman.h>
// [UPDATED] tools.hpp (line below) and, further down, Chams.h and Unity.h
// all use symbols (OBFUSCATE, Vector3) that they themselves don't define —
// they expect whatever includes them to have brought those in first. That
// held when this header was only ever reached after importz.h had already
// run, but imports.h is also included directly (layout.h, cheat.cpp), so it
// must be self-sufficient. Confirmed via the actual CI log: "use of
// undeclared identifier 'OBFUSCATE'" in tools.hpp/Chams.h, and "unknown type
// name 'Vector3'" in Quaternion.h (pulled in by Unity.h).
#include <obfuscate.h>
#include <unity/Vector3.hpp>
#include <tools.hpp>
#include <zygisk.hpp>
#include <cmath>
#include <ctime>
#include <chrono>
#include <EGL/egl.h>
#include <cstdlib>
#include <mutex>
#include <And64InlineHook.hpp>
// [UPDATED] was <chams.h>/<unity.h> (lowercase) — the real files are
// gsmodz/Chams.h and others/unity/Unity.h (capital C/U). Both resolve
// fine on a case-insensitive filesystem but fail on Linux CI, same class
// of bug as the Il2Cpp.h/Il2cpp.h fix elsewhere.
#include <Chams.h>


#include <Unity.h>
#include <imagem.h>




