cmake_minimum_required(VERSION 3.20)

# Find Rust and Cargo
find_program(CARGO_EXECUTABLE cargo REQUIRED)
find_program(RUSTC_EXECUTABLE rustc REQUIRED)

# Set target triple based on architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(RUST_TARGET "x86_64-unknown-none")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Rust library target
set(RUST_LIB_NAME "voidframe_mm")
set(RUST_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/target/${RUST_TARGET}/release/lib${RUST_LIB_NAME}.a")

# Custom command to build Rust library
add_custom_command(
    OUTPUT ${RUST_LIB_PATH}
    COMMAND ${CARGO_EXECUTABLE} build --release --target ${RUST_TARGET}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
            ${CMAKE_CURRENT_SOURCE_DIR}/src/lib.rs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/backend.rs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/rust_heap.rs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ffi.rs
    COMMENT "Building Rust heap library"
)

# Create imported library target
add_custom_target(rust_heap_build DEPENDS ${RUST_LIB_PATH})

add_library(rust_heap STATIC IMPORTED GLOBAL)
set_target_properties(rust_heap PROPERTIES
    IMPORTED_LOCATION ${RUST_LIB_PATH}
)

add_dependencies(rust_heap rust_heap_build)

# Install header
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../KernelHeapRust.h
        DESTINATION include/mm)