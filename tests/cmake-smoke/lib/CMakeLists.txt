add_library(smoke SHARED)
# globs the sources of this folder, enables AUTOMOC/AUTOUIC/AUTORCC, builds translations
mo2_configure_target(smoke WARNINGS ON)
target_include_directories(smoke PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(smoke PUBLIC Qt6::Core)
