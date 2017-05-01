cmake_minimum_required(VERSION 3.4.1)
include_directories(${PROJECT_SOURCE_DIR}/include)
##################################################
set(THREAD_TEST_SRC ThreadTest.cpp)
add_executable(threadtest ${THREAD_TEST_SRC})
target_link_libraries(threadtest vutils vthreads)
##################################################
set(STRONG_POINTER_TEST_SRC StrongPointerTest.cpp)
add_executable(strongpointerTest ${STRONG_POINTER_TEST_SRC})
target_link_libraries(strongpointerTest vutils vthreads)
##################################################
set(SERVER_TEST_SRC TestServer.cpp)
add_executable(TestServer ${SERVER_TEST_SRC})
target_link_libraries(TestServer vthreads vutils  vnetwork)





