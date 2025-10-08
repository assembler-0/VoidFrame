# Set target triple based on architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(RUST_TARGET "x86_64-unknown-none")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Rust library target
set(RUST_LIB_NAME "voidframe_spinlock")
set(RUST_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/target/${RUST_TARGET}/release/lib${RUST_LIB_NAME}.a")

# Custom command to build Rust library
add_custom_command(
    OUTPUT ${RUST_LIB_PATH}
    COMMAND ${CARGO_EXECUTABLE} build --release --target ${RUST_TARGET}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
            ${CMAKE_CURRENT_SOURCE_DIR}/src/lib.rs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/spinlock.rs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ffi.rs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/rwlock.rs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/mcs.rs
    COMMENT "Building Rust spinlock library"

)

# Create imported library target
add_custom_target(rust_spinlock_build DEPENDS ${RUST_LIB_PATH})

# Create imported library target
add_library(rust_spinlock STATIC IMPORTED GLOBAL)
set_target_properties(rust_spinlock PROPERTIES
    IMPORTED_LOCATION ${RUST_LIB_PATH}
)

# Add dependency to ensure Rust library is built first
add_dependencies(rust_spinlock rust_spinlock_build)