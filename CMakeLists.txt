cmake_minimum_required(VERSION 3.10)

project(bedrock)
add_executable(bedrock main.cpp)
set_property(TARGET bedrock PROPERTY CXX_STANDARD 17)
install(TARGETS bedrock)
