#ifndef KONSOLE_VERSION_H
#define KONSOLE_VERSION_H

/* Konsole (library) version */
#define KONSOLE_VERSION_MAJOR 1
#define KONSOLE_VERSION_MINOR 0
#define KONSOLE_VERSION_PATCH 0
#define KONSOLE_VERSION_STRING "1.0.0"

/* Firmware identity (can be overridden via build flags) */
#ifndef KONSOLE_FW_NAME
#define KONSOLE_FW_NAME "unknown-firmware"
#endif

#ifndef KONSOLE_FW_VERSION
#define KONSOLE_FW_VERSION "0.0.0"
#endif

#endif


/*

Use with :
    PIO;

build_flags =
  -Isrc/konsole
  -DKONSOLE_FW_NAME="\"my-device\""
  -DKONSOLE_FW_VERSION="\"1.7.3\""


    CMake;
target_compile_definitions(your_target PRIVATE
  KONSOLE_FW_NAME="\"my-device\""
  KONSOLE_FW_VERSION="\"1.7.3\"")

*/