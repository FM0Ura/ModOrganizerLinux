cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Concurrent)
find_path(BEXT_DI_INCLUDE_DIRS "boost/di.hpp")
find_package(mo2-cmake CONFIG REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)
if (WIN32)
	find_package(mo2-libbsarch CONFIG REQUIRED)
	find_package(mo2-dds-header CONFIG REQUIRED)
	find_package(DirectXTex CONFIG REQUIRED)
else()
	# Linux (spec of Phase 4e, section 3): libbsarch is Windows-only; ../linux has a
	# replacement of the libbsarch headers that the sources include, written on libbsarchpp
	find_package(libbsarchpp CONFIG REQUIRED)
endif()

add_library(bsa_packer SHARED)
mo2_configure_plugin(bsa_packer WARNINGS OFF)

if (WIN32)
	target_link_libraries(bsa_packer PUBLIC mo2::uibase Qt6::Concurrent mo2::libbsarch Microsoft::DirectXTex)
else()
	# BSArchive*.cpp wrap the C API of libbsarch, which Linux does not have
	get_target_property(_bsa_packer_sources bsa_packer SOURCES)
	list(FILTER _bsa_packer_sources EXCLUDE REGEX "/BSArchive[^/]*\\.cpp$")
	set_property(TARGET bsa_packer PROPERTY SOURCES ${_bsa_packer_sources})
	target_sources(bsa_packer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../linux/bs_archive_auto.cpp)
	target_include_directories(bsa_packer BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../linux/include)
	target_link_libraries(bsa_packer PUBLIC mo2::uibase Qt6::Concurrent libbsarchpp)
	# mo2_configure_plugin hides the symbols that the sources do not export with
	# BSAPACKER_EXPORT; the tests link the plugin and use some of them (vtable/typeinfo of
	# IEmitsValueChanged, IArchiveBuilder, NullArchiveBuilder) and the linux/ classes
	set_target_properties(bsa_packer PROPERTIES CXX_VISIBILITY_PRESET default)
endif()
target_include_directories(bsa_packer PUBLIC ${BEXT_DI_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(bsa_packer PRIVATE BSAPACKER_LIBRARY)

if (WIN32)
	# need to deploy this here as MO2 does not depends on Qt6::Concurrent
	install(FILES $<TARGET_FILE:Qt6::Concurrent> DESTINATION bin/dlls)
endif()

# this is done by modorganizer itself now
#
# install(FILES $<TARGET_FILE:mo2::libbsarch> DESTINATION bin/dlls)

mo2_install_plugin(bsa_packer)
