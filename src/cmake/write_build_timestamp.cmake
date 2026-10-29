if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()
get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
string(TIMESTAMP build_timestamp "%Y-%m-%d %H:%M:%S UTC" UTC)
file(WRITE "${OUTPUT}"
    "#pragma once\nnamespace cardwirth::app {\ninline constexpr char kBuildTimestamp[] = \"${build_timestamp}\";\n}\n")
