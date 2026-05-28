cmake_minimum_required(VERSION 4.0)
project(water_quality C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

add_executable(water_quality
        main.c
        pojo/Data.h
        pojo/User.h
        controller/DataController.c
        controller/DataController.h
        service/DataService.c
        service/DataService.h
        view/DataView.c
        view/DataView.h
        util/operation_file.c)

if (NOT MSVC)
    target_link_libraries(water_quality m)
endif()
