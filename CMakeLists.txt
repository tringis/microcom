project(microcom)

cmake_minimum_required(VERSION 3.10)

add_executable(microcom microcom.c)

install(TARGETS microcom DESTINATION bin)
install(FILES microcom.1 DESTINATION share/man/man1)
