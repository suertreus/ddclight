cmake_minimum_required(VERSION 3.10)

project(ddclight)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig)
find_package(absl REQUIRED)
find_package(sdbus-c++-tools REQUIRED)
find_package(sdbus-c++ REQUIRED)
pkg_check_modules(WAYLAND wayland-client)

add_custom_command(
    OUTPUT ddclight-client-glue.h ddclight-server-glue.h
    COMMAND SDBusCpp::sdbus-c++-xml2cpp ${PROJECT_SOURCE_DIR}/ddclight.xml
        --proxy=ddclight-client-glue.h --adaptor=ddclight-server-glue.h
    DEPENDS ddclight.xml
    COMMENT "Generating D-Bus interfaces for ${PROJECT_NAME}."
)

add_executable(
    ddclight
    control-backlight.cc control.cc control-ddc-i2c.cc ddclight.cc enumerate.cc fd-holder.cc misc.cc output.cc server.cc
    client.h control-backlight.h control-ddc-i2c.h control.h deleter.h enumerate.h fd-holder.h misc.h output.h server.h state.h
    ${CMAKE_CURRENT_BINARY_DIR}/ddclight-client-glue.h ${CMAKE_CURRENT_BINARY_DIR}/ddclight-server-glue.h
)

target_include_directories(
    ddclight
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE   ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(ddclight PRIVATE SDBusCpp::sdbus-c++ absl::str_format absl::strings absl::status absl::statusor absl::time absl::span absl::synchronization absl::core_headers absl::any_invocable absl::function_ref wayland-client)

install(TARGETS ddclight)
install(FILES ddclight.service DESTINATION share/dbus-1/services)
install(FILES ddclight.xml DESTINATION share/dbus-1/interfaces)
