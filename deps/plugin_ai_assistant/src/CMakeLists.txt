cmake_minimum_required(VERSION 3.16)

add_custom_target(ai_assistant ALL)
mo2_configure_python(ai_assistant MODULE)

# mo2_configure_python installs only the .py files: the icon of the tool menu goes too
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ui/icons/ai_assistant.svg"
    DESTINATION "${MO2_INSTALL_BIN}/plugins/ai_assistant/ui/icons")
