cmake_minimum_required(VERSION 3.4.1)
include_directories(${PROJECT_SOURCE_DIR}/include)
set(LIB_VUTILS_SRC 
    FileUtils.cpp 
    Logger.cpp 
    RefBase.cpp 
    StringUtils.cpp)
#aux_source_directory(. LIB_THREADS_SRC)
#add_library(vutils_static STATIC ${LIB_VUTILS_SRC})
add_library(vutils SHARED ${LIB_VUTILS_SRC})
target_link_libraries(vutils pthread vthreads)
#add_dependencies(vutils vthreads)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/output/libs)
#set_target_properties(vutils_static PROPERTIES OUTPUT_NAME "vutils")
