add_executable(vm main.cpp)

set_property(TARGET vm PROPERTY CXX_STANDARD 20)

find_package(Microsoft.GSL CONFIG REQUIRED)
target_link_libraries(vm PRIVATE Microsoft.GSL::GSL)
