add_executable(lumine2024_shell
    builtin_functions.cpp
    execution.cpp
    main.cpp
    parser.cpp
    types.cpp
)

install(TARGETS lumine2024_shell
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
