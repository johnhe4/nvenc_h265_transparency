# Create a build folder and build from there:
#   mkdir build
#   cd build
#   cmake ..

cmake_minimum_required( VERSION 3.14 )

# Apple stuff to avoid a warning about rpaths
cmake_policy( SET CMP0042 NEW )

# Filename extensions must be explicit
if ( ${CMAKE_VERSION} VERSION_GREATER "3.20" )
   cmake_policy( SET CMP0115 NEW )
endif()

# Target the 64-bit compiler on Windows. We assume 64-bit for everything
# and no effort has been made to support 32-bit.
if ( WIN32 )
   set( CMAKE_GENERATOR_PLATFORM x64 CACHE INTERNAL "Force 64-bit compilation" )
endif()

# Hardcode CUDA paths since auto detection failed for me
SET( CMAKE_CUDA_COMPILER /usr/local/cuda/bin/nvcc )
include_directories( /usr/local/cuda/include )

# Main project
project( nvenc_h265_transparency LANGUAGES CXX CUDA )

# Always look in /usr/local/include because this isn't default - sometimes.
# Also check our code's root directory.
# If this doesn't make sense for your platorm/configuration then it will be safely ignored.
SET( CMAKE_REQUIRED_INCLUDES /usr/local/include ${PROJECT_SOURCE_DIR} )
include_directories( ${CMAKE_REQUIRED_INCLUDES} )

# C++ settings
set( CMAKE_CXX_STANDARD 14 )
set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG" )

include( CheckIncludeFile )

# Use CLI11 for argument parsing
find_package( CLI11 REQUIRED )

add_executable( nvenc_h265_transparency main.cpp utility.hpp nvEncodeAPI.h )

# Link local stuff first, then system stuff

target_link_libraries( nvenc_h265_transparency nvidia-encode nvcuvid cuda )