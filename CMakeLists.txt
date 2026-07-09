cmake_minimum_required(VERSION 3.14)
project(HashwebUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

logos_module(
    NAME hashweb_ui
    REP_FILE src/HashwebBackend.rep
    SOURCES
        src/HashwebBackend.h
        src/HashwebBackend.cpp
        src/BlockListModel.h
        src/BlockListModel.cpp
        src/PageListModel.h
        src/PageListModel.cpp
    INCLUDE_DIRS
        src
)

# SpanHighlighter runs in the VIEW process (the QML scene lives there, not
# in ui-host), so it ships in the replica factory dylib — the one library
# the view process loads from this module.
find_package(Qt6 REQUIRED COMPONENTS Quick)
target_sources(hashweb_ui_replica_factory PRIVATE
    src/SpanHighlighter.h
    src/SpanHighlighter.cpp
)
target_link_libraries(hashweb_ui_replica_factory PRIVATE Qt6::Quick)
