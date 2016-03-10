project (game)

include_directories(${ANGELSCRIPT_INCLUDE_DIR})

file(GLOB GAME_HEADERS
    "*.h"
    "ai/*.h"
    "../gameshared/*.h"
    "../qcommon/cjson.h"
    "../qalgo/base64.h"
    "../qalgo/sha2.h"
)

file(GLOB GAME_SOURCES
    "*.cpp"
    "ai/*.cpp"
    "../gameshared/*.c"
    "../matchmaker/mm_rating.c"
    "../qcommon/cjson.c"
    "../qalgo/base64.c"
    "../qalgo/sha2.c"
)

add_library(game SHARED ${GAME_SOURCES} ${GAME_HEADERS})
target_link_libraries(game PRIVATE ${ANGELSCRIPT_LIBRARY})
add_dependencies(game angelwrap)
set_property(TARGET game PROPERTY CXX_STANDARD 11)
qf_set_output_dir(game ${QFUSION_GAME_DIR})
