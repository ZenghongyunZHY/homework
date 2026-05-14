cmake_minimum_required(VERSION 4.0)
project(homework C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

add_executable(homework
        main.h
        mian.c
        operation_file.c
        operation_file.h
        pojo/Data.h
        pojo/User.h
        controller/DataController.c
        controller/DataController.h
        service/DataService.c
        service/DataService.h
        view/DataView.c
        view/DataView.h)
