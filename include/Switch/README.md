# Enhanced Switch - Rust-like Pattern Matching for C

A powerful macro-based pattern matching system that brings Rust-like `match` capabilities to C while maintaining freestanding compatibility.

## Basic Usage

### Standard Switch
```c
SWITCH(value) {
    CASE(1) {
        printf("One\n");
    }
    CASE(2) {
        printf("Two\n");
    }
    DEFAULT {
        printf("Other\n");
    }
END_SWITCH
```

### Range Matching
```c
SWITCH(grade) {
    RANGE(90, 100) {
        printf("A\n");
    }
    RANGE(80, 89) {
        printf("B\n");
    }
    RANGE(70, 79) {
        printf("C\n");
    }
END_SWITCH
```

### Multiple Values
```c
SWITCH(day) {
    CASE_ANY(1, 2, 3, 4, 5) {
        printf("Weekday\n");
    }
    CASE_ANY(6, 7) {
        printf("Weekend\n");
    }
END_SWITCH
```

## Advanced Patterns

### Conditional Matching
```c
SWITCH(x) {
    WHEN(x > 0 && x < 10) {
        printf("Single digit positive\n");
    }
    WHEN(x % 2 == 0) {
        printf("Even number\n");
    }
END_SWITCH
```

### Bitwise Patterns
```c
SWITCH(flags) {
    CASE_BITS(0xFF00, 0x1200) {
        printf("High byte is 0x12\n");
    }
    CASE_BITS(0x0001, 0x0001) {
        printf("Bit 0 is set\n");
    }
END_SWITCH
```

### Predicate Functions
```c
bool is_prime(int n) { /* implementation */ }

SWITCH(number) {
    CASE_IF(is_prime) {
        printf("Prime number\n");
    }
    CASE_IF(is_even) {
        printf("Even number\n");
    }
END_SWITCH
```

## String Matching

### Basic String Cases
```c
STR_SWITCH(command) {
    STR_CASE("help") {
        show_help();
    }
    STR_CASE("quit") {
        exit_program();
    }
    DEFAULT {
        printf("Unknown command\n");
    }
END_SWITCH
```

### Multiple String Values
```c
STR_SWITCH(input) {
    STR_CASE_ANY("y", "yes", "Y", "YES", NULL) {
        printf("Confirmed\n");
    }
    STR_CASE_ANY("n", "no", "N", "NO", NULL) {
        printf("Denied\n");
    }
END_SWITCH
```

### Prefix Matching
```c
STR_SWITCH(filename) {
    STR_PREFIX("temp_") {
        printf("Temporary file\n");
    }
    STR_PREFIX("backup_") {
        printf("Backup file\n");
    }
END_SWITCH
```

## Pointer Patterns

### Null Safety
```c
PTR_SWITCH(ptr) {
    PTR_NULL {
        printf("Null pointer\n");
    }
    PTR_VALID {
        printf("Valid pointer: %p\n", ptr);
        // Use ptr safely here
    }
END_SWITCH
```

## Array Patterns

### Size-based Matching
```c
ARRAY_SWITCH(buffer, buffer_len) {
    ARRAY_EMPTY {
        printf("Empty array\n");
    }
    ARRAY_SIZE(1) {
        printf("Single element\n");
    }
    DEFAULT {
        printf("Multiple elements: %zu\n", _arr_len);
    }
END_SWITCH
```

## Struct Destructuring

### Field Extraction
```c
typedef struct { int x, y; } Point;
Point p = {10, 20};

MATCH_STRUCT(p) {
    EXTRACT(x, px);
    EXTRACT(y, py);
    WHEN(px > 0 && py > 0) {
        printf("Point in first quadrant: (%d, %d)\n", px, py);
    }
END_SWITCH
```

## Result/Option Patterns

### Error Handling
```c
typedef struct {
    bool is_ok;
    union {
        int value;
        int error;
    };
} Result;

Result result = get_result();

RESULT_SWITCH(&result) {
    OK(val) {
        printf("Success: %d\n", val);
    }
    ERR(err) {
        printf("Error: %d\n", err);
    }
END_SWITCH
```

## Control Flow

### Fallthrough
```c
SWITCH(value) {
    CASE(1) {
        printf("One ");
        FALLTHROUGH
    }
    CASE(2) {
        printf("or Two\n");
    }
END_SWITCH
```

### Multiple Conditions
```c
SWITCH(x) {
    CASE_ALL(x > 0, x < 100, x % 5 == 0) {
        printf("Positive multiple of 5 under 100\n");
    }
END_SWITCH
```

### Early Exit
```c
SWITCH(value) {
    CASE(1) {
        if (error_condition) {
            BREAK_SWITCH;
        }
        printf("Processing...\n");
    }
END_SWITCH
```