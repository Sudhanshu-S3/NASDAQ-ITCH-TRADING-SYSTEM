add_library(nts_common STATIC
    file_buffer.cc
    timing.cc)
target_include_directories(nts_common PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(nts_common PUBLIC cxx_std_20)
