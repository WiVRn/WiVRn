add_library(reed-solomon STATIC rs.c rs.h)
target_include_directories(reed-solomon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
