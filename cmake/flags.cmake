# ============================================================================
# Compiler Flags
# ============================================================================
set(C_FLAGS " -m64 -target x86_64-unknown-none-elf -O2 -fno-omit-frame-pointer -finline-functions -foptimize-sibling-calls -nostdinc -nostdlib -fno-builtin -ffreestanding -mno-red-zone -mserialize -fPIE -fPIC -mcmodel=kernel -fcf-protection=full -fvisibility=hidden")

if(SILENT_BUILD)
    string(APPEND C_FLAGS " -w")
    string(APPEND CMAKE_ASM_NASM_FLAGS " -w-all -Wno-all")
    set(Corrosion_CARGO_BUILD_FLAGS FLAGS "--quiet")
else()
    set(Corrosion_CARGO_BUILD_FLAGS "")
endif()

if(STACK_PROTECTION)
    string(APPEND C_FLAGS " -fstack-protector-strong -D_FORTIFY_SOURCE=2")
endif()

if(DEBUG_SYMBOLS)
    string(APPEND C_FLAGS " -g3 -DDEBUG")
    string(APPEND CMAKE_ASM_NASM_FLAGS " -g -O0")
endif()

set(CMAKE_C_FLAGS "${C_FLAGS}")
set(CMAKE_CXX_FLAGS "${C_FLAGS} -fno-exceptions -fno-rtti -fno-threadsafe-statics")

set(ASM_NASM_FLAGS "-f elf64")
if(VF_CONFIG_VESA_FB)
    string(APPEND ASM_NASM_FLAGS " -DVF_CONFIG_VESA_FB")
endif()
set(CMAKE_ASM_NASM_FLAGS "${ASM_NASM_FLAGS}")