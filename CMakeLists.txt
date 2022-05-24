cmake_minimum_required(VERSION 3.10)

project(bedrock)
add_executable(vm main.cpp)
set_property(TARGET vm PROPERTY CXX_STANDARD 17)
