cmake_minimum_required(VERSION 3.8)

project(BetterSunshineMoveset VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)

file(GLOB BETTER_MOVESET_SRC
    "src/*.c"
    "src/*.cpp"
    "src/moveset/*.c"
    "src/moveset/*.cpp"
    "include/*.h"
    "include/*.hxx"
)

add_executable(BetterSunshineMoveset ${BETTER_MOVESET_SRC})
add_subdirectory(lib/BetterSunshineEngine)

target_link_libraries(BetterSunshineMoveset PUBLIC BetterSunshineEngine)

target_include_directories(BetterSunshineMoveset PUBLIC "include")

target_compile_options(BetterSunshineMoveset PUBLIC ${SMS_COMPILE_FLAGS})
target_link_options(BetterSunshineMoveset PUBLIC ${SMS_LINK_FLAGS})

list(APPEND BETTER_MOVESET_CONFIG_DEFINES "KURIBO_NO_TYPES" "BETTER_MOVESET_VERSION=\"v1.1\"")

target_compile_definitions(BetterSunshineMoveset PUBLIC ${BETTER_MOVESET_CONFIG_DEFINES})

add_custom_target(${PROJECT_NAME}.kxe ALL DEPENDS ${PROJECT_NAME})
add_custom_command(
    TARGET ${PROJECT_NAME}.kxe
    COMMAND ${PROJECT_SOURCE_DIR}/lib/BetterSunshineEngine/tools/KuriboConverter.exe ARGS ${PROJECT_NAME} ${PROJECT_NAME}.kxe ${PROJECT_SOURCE_DIR}/lib/BetterSunshineEngine/maps/${SMS_REGION}.map
)