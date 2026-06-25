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
        service/field_meta.c
        service/dataset.c
        service/io_csv.c
        service/io_marks.c
        service/analyze.c
        service/auth.c
        service/storage_backup.c
        service/reports.c
        service/DataService.h
        service/DataServiceInternal.h
        view/view_util.c
        view/view_login.c
        view/view_crud.c
        view/view_analyze.c
        view/view_report.c
        view/view_backup.c
        view/DataView.h
        view/DataViewInternal.h
        util/operation_file.c
        util/operation_file.h)

if (NOT MSVC)
    target_link_libraries(water_quality m)
endif()
