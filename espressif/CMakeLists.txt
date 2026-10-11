# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.19)

# The stage binary doesn't depend on the payload: it fetches the payload's MD5 and size from the
# server (/payload.md5) at runtime, and checks the write ends below its own partition before erasing.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espreswitch)
