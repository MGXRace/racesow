#!/bin/bash
if [ -z "$1" ]; then
    echo "Specify target OS"
    exit 1
fi

if [ -z "$2" ]; then
    echo "Specify target arch"
    exit 1
fi

OS="$1"
ARCH="$2"

if [ "$OS" = "lin" ]; then
    echo "Dynamic linking is used for $OS, skipping"
    exit 0
fi

set -e

. `pwd`/inc/common.inc.sh
. `pwd`/inc/target-${OS}-${ARCH}.inc.sh

TARGET_DIR="${SOURCE_DIR}/source/$OS/$ARCH/$TLIB_DIR/"
if [ ! -d "$TARGET_DIR" ]; then
    mkdir -p "$TARGET_DIR"
fi

COMMAND_PREF="PATH=\"${PATH}\" HOST=${HOST} SOURCE_DIR=\"${SOURCE_DIR}\" TARGET_DIR=\"${TARGET_DIR}\" CHOST=${HOST}  CFLAGS=\"${CFLAGS_COMMON}\" MAKE=\"${MAKE}\" ARCH=\"$ARCH\" "
if [ ! -z "$DATA_DIR" ]; then
    COMMAND_PREF="${COMMAND_PREF} DATA_DIR=\"${DATA_DIR}\""
fi
if [ ! -z "$MINGW_DIR" ]; then
    COMMAND_PREF="${COMMAND_PREF} LIB_DIR=\"${MINGW_DIR}/lib\" INCLUDE_DIR=\"${MINGW_DIR}include\""
fi
COMMAND_PREF="${COMMAND_PREF} ENABLE_SHARED=$ENABLE_SHARED SHARED_LIBRARY_EXT=$SHARED_LIBRARY_EXT"


# zlib
if [ -z "$3" ] || [ "$3" == "zlib" ]; then
    COMMAND="${COMMAND_PREF} "
    if [ ! -z "$MINGW_DIR" ]; then
        COMMAND="${COMMAND} PATH=\"${MINGW_DIR}mingw/bin:$PATH\""
        COMMAND="${COMMAND} CC=\"${HOST_PREF}gcc\" RC=\"${HOST_PREF}windres\" DLLWRAP=\"${HOST_PREF}dllwrap\" STRIP=\"${HOST_PREF}strip\" AR=\"${HOST_PREF}ar\""
    fi
    COMMAND="${COMMAND} ./libs/zlib.sh"
    echo "$COMMAND" && eval $COMMAND
fi

# libcurl
if [ -z "$3" ] || [ "$3" == "curl" ]; then
    COMMAND="${COMMAND_PREF}"
    if [ "$OS" = "win32" ]; then
        COMMAND="${COMMAND} ENABLE_WINSSL=YES"
    fi
    COMMAND="${COMMAND} ./libs/curl.sh"
    echo "$COMMAND" && eval $COMMAND
fi

# libjpeg
if [ -z "$3" ] || [ "$3" == "jpeg" ]; then
    COMMAND="${COMMAND_PREF} CFLAGS=\"${CFLAGS_COMMON}\""
    COMMAND="${COMMAND} ./libs/jpeg.sh"
    echo "$COMMAND" && eval $COMMAND
fi

# libogg
if [ -z "$3" ] || [ "$3" == "ogg" ]; then
    COMMAND="${COMMAND_PREF}"
    COMMAND="${COMMAND} ./libs/ogg.sh"
    echo "$COMMAND" && eval $COMMAND
fi

# libvorbis
if [ -z "$3" ] || [ "$3" == "vorbis" ]; then
    COMMAND="${COMMAND_PREF}"
    COMMAND="${COMMAND} ./libs/vorbis.sh"
    echo "$COMMAND" && eval $COMMAND
fi

# libtheora
if [ -z "$3" ] || [ "$3" == "theora" ]; then
    COMMAND="${COMMAND_PREF}"
    COMMAND="${COMMAND} ./libs/theora.sh"
    echo "$COMMAND" && eval $COMMAND
fi

# libfreetype
if [ -z "$3" ] || [ "$3" == "freetype" ]; then
    COMMAND="${COMMAND_PREF}"
    COMMAND="${COMMAND} ./libs/freetype.sh"
    echo "$COMMAND" && eval $COMMAND
fi

# libpng
if [ -z "$3" ] || [ "$3" == "png" ]; then
    COMMAND="${COMMAND_PREF}"
    COMMAND="${COMMAND} ./libs/png.sh"
    echo "$COMMAND" && eval $COMMAND
fi
