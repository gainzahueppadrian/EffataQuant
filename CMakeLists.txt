cmake_minimum_required(VERSION 3.20)
project(Effata_Investments VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ultra-low latency optimization flags for cloud deployment
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-interference-size -O3 -march=native -mavx2 -mfma -mtune=native -ffast-math -funroll-loops -fomit-frame-pointer -flto -fopenmp")

find_package(Eigen3 REQUIRED NO_MODULE)
find_package(PkgConfig REQUIRED)
pkg_search_module(ZMQ REQUIRED libzmq)

# Find TBB for parallelization
find_package(TBB REQUIRED)

include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${EIGEN3_INCLUDE_DIRS}
)

add_executable(effata_investments src/main.cpp)

target_link_libraries(effata_investments
    Eigen3::Eigen
    ${ZMQ_LIBRARIES}
    TBB::tbb
)
target_include_directories(effata_investments PRIVATE ${ZMQ_INCLUDE_DIRS})
