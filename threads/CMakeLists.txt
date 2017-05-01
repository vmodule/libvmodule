cmake_minimum_required(VERSION 3.4.1)
include_directories(${PROJECT_SOURCE_DIR}/include)
set(LIB_THREADS_SRC 
    Event.cpp 
    Looper.cpp 
    Mutex.cpp 
    SystemClock.cpp
    Thread.cpp)
#aux_source_directory(. LIB_THREADS_SRC)
#add_library(vthreads_static STATIC ${LIB_THREADS_SRC})
#target_link_libraries(vthreads_static vutils_static)
add_library(vthreads SHARED ${LIB_THREADS_SRC})
target_link_libraries(vthreads pthread)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/output/libs)
#set_target_properties(vthreads_static PROPERTIES OUTPUT_NAME "vthreads")
