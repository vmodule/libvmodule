cmake_minimum_required(VERSION 3.4.1)
project(vmodule)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -UNDEBUG -g3 -O3 -Wall -fmessage-length=0 -Wno-unused-parameter -fexceptions")
add_definitions("-DTARGET_POSIX -D_DEBUG -DDEBUG_ENABLE")
SET(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR}/output)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/output/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/output/libs)
#cmake ../ -DCMAKE_INSTALL_PREFIX=../output

include_directories(${PROJECT_SOURCE_DIR}/include)
add_subdirectory(vutils)
add_subdirectory(threads)
add_subdirectory(network)
add_subdirectory(tests) 

#set(LIB_VMODULE_SRC ${CMAKE_SOURCE_DIR}/vmodule.cpp)
#add_library(vmodule SHARED ${LIB_VMODULE_SRC})

#target_link_libraries(
#    vmodule 
#    "-Wl,--whole-archive" #之后的库使用--whole-archive选项
#    libvthreads.a
#    libvutils.a
#    "-Wl,--no-whole-archive")  #之后的库不使用--whole-archive选项

#target_link_libraries(
#    vmodule 
#    "-Wl,--whole-archive" #之后的库使用--whole-archive选项
    ####
#    "-Wl,--no-whole-archive")  #之后的库不使用--whole-archive选项
#set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/output/libs)  
#aux_source_directory(${CMAKE_SOURCE_DIR}/threads THREADS_SRC)
#aux_source_directory(${CMAKE_SOURCE_DIR}/vutils VUTILS_SRC)
#set(LIB_VMODULE_SRC 
#    ${THREADS_SRC}
#    ${VUTILS_SRC}
#    ${CMAKE_SOURCE_DIR}/vmodule.cpp)

#add_library(vmodule_static STATIC ${LIB_VMODULE_SRC})
#add_library(vmodule SHARED ${LIB_VMODULE_SRC})

#target_link_libraries(vmodule_static pthread)
#target_link_libraries(vmodule pthread)

#set_target_properties(vmodule_static PROPERTIES OUTPUT_NAME "vmodule")  
   
    
INSTALL(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ 
    DESTINATION include)   
     
INSTALL(DIRECTORY ${PROJECT_BINARY_DIR}/output/libs/ 
    DESTINATION libs
    PATTERN "libs/*"
    PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ GROUP_WRITE WORLD_READ WORLD_WRITE) 

INSTALL(DIRECTORY ${PROJECT_BINARY_DIR}/output/bin/ 
    DESTINATION bin
    PATTERN "bin/*"
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ GROUP_WRITE WORLD_EXECUTE WORLD_READ WORLD_WRITE) 

