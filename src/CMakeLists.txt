
find_package(mo2-cmake CONFIG REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)

add_library(installer_bundle SHARED)
mo2_configure_plugin(installer_bundle WARNINGS OFF)
target_link_libraries(installer_bundle PRIVATE mo2::uibase)
mo2_install_plugin(installer_bundle)
