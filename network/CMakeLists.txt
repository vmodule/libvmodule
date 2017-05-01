cmake_minimum_required(VERSION 3.4.1)
include_directories(${PROJECT_SOURCE_DIR}/include)
set(LIB_NET_SRC
    core/conn_base.cpp 
    core/conn_queue.cpp
    core/conn_thread.cpp
    core/conn_wrap.cpp
    core/conn_utils.cpp
    RtspServer.cpp
    SampleServer.cpp)
     
#aux_source_directory(. LIB_THREADS_SRC)
#add_library(vnet_static STATIC ${LIB_NET_SRC})
#target_link_libraries(vnet_static 
#    vthreads_static
#    vutils_static)
    
add_library(vnetwork SHARED ${LIB_NET_SRC})
target_link_libraries(vnetwork 
    vthreads
    vutils 
    pthread
    event)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/output/libs)
#set_target_properties(vnet_static PROPERTIES OUTPUT_NAME "vnet")
