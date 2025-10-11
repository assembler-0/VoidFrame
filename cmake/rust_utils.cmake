# ============================================================================
# Rust Utilities for CMake
# ============================================================================

# Function to link Rust libraries with automatic hyphen-to-underscore conversion
function(link_rust_library target_name rust_lib_name)
    string(REPLACE "-" "_" converted_name "${rust_lib_name}")
    target_link_libraries(${target_name} PRIVATE ${converted_name})
endfunction()