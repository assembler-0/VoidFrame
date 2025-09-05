#!/usr/bin/env python3
"""
C Function Name Mangler
Obfuscates function names in C source code to make reverse engineering annoying
"""

import os
import re
import random
import string
import json
from pathlib import Path

class CNameMangler:
    def __init__(self):
        self.mapping = {}
        self.reserved_words = {
            # C keywords
            'auto', 'break', 'case', 'char', 'const', 'continue', 'default', 'do',
            'double', 'else', 'enum', 'extern', 'float', 'for', 'goto', 'if',
            'int', 'long', 'register', 'return', 'short', 'signed', 'sizeof', 'static',
            'struct', 'switch', 'typedef', 'union', 'unsigned', 'void', 'volatile', 'while',
            # Common libc functions you might not want to mangle
            'printf', 'malloc', 'free', 'memcpy', 'strlen', 'strcmp',
            # Your kernel entry points you might want to keep
            'kmain', '_start', 'kernel_main'
        }
        self.protected_functions = set()

    def generate_mangled_name(self, original_name):
        """Generate a mangled name that looks like compiler output"""
        if original_name in self.mapping:
            return self.mapping[original_name]

        # Different mangling styles
        styles = [
            self._generate_hex_style,
            self._generate_underscore_style,
            self._generate_mixed_style,
            self._generate_compiler_style
        ]

        style = random.choice(styles)
        mangled = style(original_name)

        # Ensure uniqueness
        while mangled in self.mapping.values() or mangled in self.reserved_words:
            mangled = style(original_name)

        self.mapping[original_name] = mangled
        return mangled

    def _generate_hex_style(self, name):
        """Generate names like _func_a2b3c4d5"""
        hex_suffix = ''.join(random.choices('0123456789abcdef', k=8))
        return f"_func_{hex_suffix}"

    def _generate_underscore_style(self, name):
        """Generate names like __ZN4kern5mmgr7E"""
        random_chars = ''.join(random.choices(string.ascii_lowercase + string.digits, k=8))
        return f"__ZN{len(name)}{random_chars}E"

    def _generate_mixed_style(self, name):
        """Generate names like _k7m3_fn_a4b2"""
        prefix = ''.join(random.choices(string.ascii_lowercase + string.digits, k=4))
        suffix = ''.join(random.choices(string.ascii_lowercase + string.digits, k=4))
        return f"_k{prefix}_fn_{suffix}"

    def _generate_compiler_style(self, name):
        """Generate names that look like compiler internals"""
        templates = [
            f"__builtin_{random.randint(1000, 9999)}",
            f"_internal_func_{random.randint(100, 999)}",
            f"__kern_impl_{random.randint(10, 99)}",
            f"_sys_call_impl_{random.randint(1, 256)}"
        ]
        return random.choice(templates)

    def extract_functions(self, source_code):
        """Extract function definitions and declarations"""
        # Pattern for function definitions/declarations
        # This is simplified - real C parsing is complex
        patterns = [
            # Function definitions: return_type function_name(params) {
            r'\b([a-zA-Z_][a-zA-Z0-9_]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\([^)]*\)\s*\{',
            # Function declarations: return_type function_name(params);
            r'\b([a-zA-Z_][a-zA-Z0-9_]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\([^)]*\)\s*;',
            # Static functions
            r'\bstatic\s+[a-zA-Z_][a-zA-Z0-9_]*\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\([^)]*\)',
        ]

        functions = set()
        for pattern in patterns:
            matches = re.finditer(pattern, source_code, re.MULTILINE)
            for match in matches:
                if len(match.groups()) >= 2:
                    func_name = match.group(2)
                else:
                    func_name = match.group(1)

                # Skip reserved words and protected functions
                if func_name not in self.reserved_words and func_name not in self.protected_functions:
                    functions.add(func_name)

        return functions

    def mangle_source_file(self, filepath):
        """Mangle function names in a single source file"""
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()

        # Extract functions
        functions = self.extract_functions(content)

        # Generate mappings for new functions
        for func in functions:
            if func not in self.mapping:
                self.generate_mangled_name(func)

        # Replace function names
        for original, mangled in self.mapping.items():
            # Replace function definitions and calls
            patterns = [
                # Function definition: func_name(
                (rf'\b{re.escape(original)}\s*\(', f'{mangled}('),
                # Function calls: func_name(
                (rf'\b{re.escape(original)}\b(?=\s*\()', mangled),
                # Function pointers: &func_name
                (rf'&\s*{re.escape(original)}\b', f'&{mangled}'),
            ]

            for pattern, replacement in patterns:
                content = re.sub(pattern, replacement, content)

        return content

    def mangle_project(self, source_dir, output_dir=None, file_extensions=None):
        """Mangle an entire C project"""
        if file_extensions is None:
            file_extensions = ['.c', '.h']

        if output_dir is None:
            output_dir = source_dir + '_mangled'

        Path(output_dir).mkdir(exist_ok=True)

        # Process all C files
        for root, dirs, files in os.walk(source_dir):
            for file in files:
                if any(file.endswith(ext) for ext in file_extensions):
                    source_path = os.path.join(root, file)

                    # Calculate output path
                    rel_path = os.path.relpath(source_path, source_dir)
                    output_path = os.path.join(output_dir, rel_path)

                    # Create output directory
                    os.makedirs(os.path.dirname(output_path), exist_ok=True)

                    # Mangle the file
                    mangled_content = self.mangle_source_file(source_path)

                    with open(output_path, 'w', encoding='utf-8') as f:
                        f.write(mangled_content)

                    print(f"Mangled: {source_path} -> {output_path}")

        # Save mapping for debugging
        mapping_file = os.path.join(output_dir, '_mangling_map.json')
        with open(mapping_file, 'w') as f:
            json.dump(self.mapping, f, indent=2)

        print(f"Mapping saved to: {mapping_file}")
        print(f"Total functions mangled: {len(self.mapping)}")

def main():
    import argparse

    parser = argparse.ArgumentParser(description='Mangle C function names')
    parser.add_argument('source_dir', help='Source directory to mangle')
    parser.add_argument('-o', '--output', help='Output directory')
    parser.add_argument('--protect', nargs='*', help='Functions to protect from mangling')
    parser.add_argument('--extensions', nargs='*', default=['.c', '.h'],
                        help='File extensions to process')

    args = parser.parse_args()

    mangler = CNameMangler()

    # Add protected functions
    if args.protect:
        mangler.protected_functions.update(args.protect)

    # Add kernel-specific protected functions
    kernel_protected = {
        'kmain', '_start', 'kernel_main', 'interrupt_handler',
        'page_fault_handler', 'timer_interrupt', 'keyboard_interrupt'
    }
    mangler.protected_functions.update(kernel_protected)

    mangler.mangle_project(args.source_dir, args.output, args.extensions)

if __name__ == '__main__':
    main()
