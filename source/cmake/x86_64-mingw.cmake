# cmake -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-mingw.cmake .
set(CMAKE_SYSTEM_NAME "Windows")

SET(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
SET(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
SET(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
# SET(CMAKE_AR x86_64-w64-mingw32-ar)
# SET(CMAKE_RANLIB x86_64-w64-mingw32-ranlib)
