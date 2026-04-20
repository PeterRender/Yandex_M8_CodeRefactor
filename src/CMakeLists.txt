# # THE LIST OF PLUGINS AND THE CORRESPONDING SOURCE FILES
# # ======================================================
# set(CLANG_TUTOR_PLUGINS
#     LACommenter
#     CodeStyleChecker
#     Obfuscator
#     UnusedForLoopVar
#     CodeRefactor
#     )

# set(LACommenter_SOURCES
#   LACommenter.cpp)

# set(CodeStyleChecker_SOURCES
#   CodeStyleChecker.cpp)

# set(Obfuscator_SOURCES
#   Obfuscator.cpp)

# set(UnusedForLoopVar_SOURCES
#   UnusedForLoopVar.cpp)

# set(CodeRefactor_SOURCES
#   CodeRefactor.cpp)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Создаем библиотеку из логики
add_library(refactor_tool_lib STATIC 
    RefactorTool.cpp
)

# 2. Настраиваем инклуды для библиотеки
target_include_directories(
    refactor_tool_lib
    PUBLIC  # чтобы тесты тоже видели эти инклуды
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)

# 3. Линкуем библиотеку с Clang
target_link_libraries(refactor_tool_lib PUBLIC clangTooling clangBasic clangASTMatchers)

# 4. Создаем исполняемый файл основного приложения
add_executable(refactor_tool main.cpp)

# 5. Линкуем исполняемый файл с библиотекой
target_link_libraries(refactor_tool PRIVATE refactor_tool_lib)

# # CONFIGURE THE PLUGIN LIBRARIES
# # ==============================
# foreach( plugin ${CLANG_TUTOR_PLUGINS} )
#     # Create a library corresponding to 'plugin'
#     add_library(
#       ${plugin}
#       SHARED
#       ${${plugin}_SOURCES}
#       )

#     # Configure include directories for 'plugin'
#     target_include_directories(
#       ${plugin}
#       PRIVATE
#       "${CMAKE_CURRENT_SOURCE_DIR}/../include"
#     )

#     # On Darwin (unlike on Linux), undefined symbols in shared objects are not
#     # allowed at the end of the link-edit. The plugins defined here:
#     #  - _are_ shared objects
#     #  - reference symbols from LLVM shared libraries, i.e. symbols which are
#     #    undefined until those shared objects are loaded in memory (and hence
#     #    _undefined_ during static linking)
#     # The build will fail with errors like this:
#     #    "Undefined symbols for architecture x86_64"
#     # with various LLVM symbols being undefined. Since those symbols are later
#     # loaded and resolved at runtime, these errors are false positives.
#     # This behaviour can be modified via the '-undefined' OS X linker flag as
#     # follows.
#     target_link_libraries(
#       ${plugin}
#       "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>"
#       )
# endforeach()
