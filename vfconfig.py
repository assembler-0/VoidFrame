#!/usr/bin/env python3
"""
Meson Config Flag Editor - A TUI for managing kernel config flags in meson.build
"""

import re
import os
import sys
from typing import List, Tuple, Optional
from dataclasses import dataclass
from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical, ScrollableContainer
from textual.widgets import Header, Footer, Static, Checkbox, Button, Label
from textual.binding import Binding
from textual.screen import ModalScreen
from textual.widgets import Input
from textual import on
from textual.message import Message

@dataclass
class ConfigFlag:
    name: str
    enabled: bool
    description: str = ""
    line_number: int = 0
    original_line: str = ""

class SaveConfirmScreen(ModalScreen[bool]):
    """Modal screen for confirming save operation."""

    BINDINGS = [
        ("y", "confirm", "Yes"),
        ("n", "cancel", "No"),
        ("escape", "cancel", "Cancel"),
    ]

    def compose(self) -> ComposeResult:
        yield Vertical(
            Static("Save changes to meson.build?", classes="question"),
            Horizontal(
                Button("Yes", variant="success", id="yes"),
                Button("No", variant="error", id="no"),
                classes="buttons",
            ),
            classes="dialog",
        )

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "yes":
            self.dismiss(True)
        else:
            self.dismiss(False)

    def action_confirm(self) -> None:
        self.dismiss(True)

    def action_cancel(self) -> None:
        self.dismiss(False)

class ConfigFlagWidget(Static):
    """Widget representing a single config flag with checkbox and description."""

    def __init__(self, flag: ConfigFlag, **kwargs):
        super().__init__(**kwargs)
        self.flag = flag

    def compose(self) -> ComposeResult:
        with Horizontal():
            checkbox = Checkbox(self.flag.name, self.flag.enabled, id=f"check_{self.flag.name}")
            yield checkbox
            if self.flag.description:
                yield Label(f"- {self.flag.description}", classes="flag-desc")

    @on(Checkbox.Changed)
    def on_checkbox_changed(self, event: Checkbox.Changed) -> None:
        self.flag.enabled = event.value
        self.post_message(ConfigFlagChanged(self.flag))

class ConfigFlagChanged(Message):
    """Message sent when a config flag is toggled."""

    def __init__(self, flag: ConfigFlag):
        self.flag = flag
        super().__init__()

class MesonConfigEditor(App):
    """Main application for editing meson config flags."""

    CSS = """
    .dialog {
        align: center middle;
        background: $surface;
        border: thick $primary;
        width: 50%;
        height: auto;
        padding: 1;
    }
    
    .question {
        text-align: center;
        margin: 1;
    }
    
    .buttons {
        align: center middle;
        height: auto;
    }
    
    .flag-name {
        min-width: 40;
        color: $accent;
    }
    
    .flag-desc {
        color: $text-muted;
        text-style: italic;
    }
    
    /* This was the cause of the spacing issue. It has been removed. */
    /* .flag-container {
        height: 1;
    } */
    
    #stats_display {
        background: $panel;
        padding: 1;
        margin-bottom: 1;
    }
    
    .container {
        padding: 1;
    }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("s", "save", "Save"),
        ("r", "reload", "Reload"),
        ("a", "toggle_all", "Toggle All"),
        ("?", "help", "Help"),
    ]

    def __init__(self, filename: str = "meson.build"):
        super().__init__()
        self.filename = filename
        self.flags: List[ConfigFlag] = []
        self.original_content: List[str] = []
        self.flag_widgets: List[ConfigFlagWidget] = []

    def compose(self) -> ComposeResult:
        yield Header()
        with Vertical(classes="container"):
            yield Static(f"Editing: {self.filename}")
            yield Static(id="stats_display")
            with ScrollableContainer(id="flag_container"):
                yield Static("Loading...")
        yield Footer()

    def on_mount(self) -> None:
        self.load_file()

    def load_file(self) -> None:
        try:
            with open(self.filename, 'r') as f:
                self.original_content = f.readlines()
            self.flags = self.parse_config_flags(self.original_content)
            self.update_display()
        except FileNotFoundError:
            self.notify(f"Error: {self.filename} not found!", severity="error")
        except Exception as e:
            self.notify(f"Error loading file: {e}", severity="error")

    def parse_config_flags(self, lines: List[str]) -> List[ConfigFlag]:
        """Parse config flags from meson.build content using a robust regex."""
        flags = []
        in_config_array = False
        array_pattern = r'vf_config_flags\s*=\s*\['
        # Regex to capture the flag line structure
        # Group 1: Optional leading '#'
        # Group 2: The flag name inside '-D' and quotes
        # Group 3: Optional description comment
        flag_pattern = re.compile(r"^\s*(#)?\s*'-D([^']+)'.*?(#\s*(.*))?$")

        for i, line in enumerate(lines):
            line_content = line.strip()
            if re.search(array_pattern, line_content):
                in_config_array = True
                continue
            if in_config_array:
                if ']' in line_content:
                    break

                match = flag_pattern.match(line_content)
                if match:
                    is_commented = match.group(1) is not None
                    flag_name = match.group(2)
                    description = match.group(4).strip() if match.group(4) else ""

                    flags.append(ConfigFlag(
                        name=flag_name,
                        enabled=not is_commented,
                        description=description,
                        line_number=i,
                        original_line=line
                    ))
        return flags

    def update_display(self) -> None:
        """Update the display with current flags."""
        container = self.query_one("#flag_container")
        container.remove_children()
        self.flag_widgets.clear()

        if not self.flags:
            container.mount(Static("No config flags found in vf_config_flags array."))
            return

        self.update_stats()

        for flag in self.flags:
            widget = ConfigFlagWidget(flag)
            self.flag_widgets.append(widget)
            container.mount(widget)
        self.notify(f"Loaded {len(self.flags)} flags.")

    def update_stats(self) -> None:
        """Update the statistics display."""
        if not self.flags:
            return
        enabled_count = sum(1 for f in self.flags if f.enabled)
        stats_text = f"Total: {len(self.flags)} | Enabled: {enabled_count} | Disabled: {len(self.flags) - enabled_count}"
        self.query_one("#stats_display", Static).update(stats_text)

    @on(ConfigFlagChanged)
    def on_config_flag_changed(self, message: ConfigFlagChanged) -> None:
        """Handle when a config flag is toggled by updating stats."""
        self.update_stats()

    def action_save(self) -> None:
        """Save changes to the file."""
        async def _save():
            confirmed = await self.push_screen_wait(SaveConfirmScreen())
            if confirmed:
                try:
                    new_content = self.generate_updated_content()
                    with open(self.filename, 'w') as f:
                        f.write(new_content)
                    self.notify("File saved successfully!")
                    self.load_file() # Reload to confirm changes
                except Exception as e:
                    self.notify(f"Error saving file: {e}", severity="error")
        self.run_worker(_save())

    def generate_updated_content(self) -> str:
        """Generate updated file content by modifying lines based on flag state."""
        lines = list(self.original_content) # Make a copy

        for flag in self.flags:
            line_num = flag.line_number
            original_line = lines[line_num]
            stripped_line = original_line.lstrip()

            is_currently_commented = stripped_line.startswith('#')

            # Case 1: Flag should be enabled, but is currently commented
            if flag.enabled and is_currently_commented:
                # Find first '#' and remove it
                lines[line_num] = original_line.replace('#', '', 1)

            # Case 2: Flag should be disabled, but is currently not commented
            elif not flag.enabled and not is_currently_commented:
                indent_len = len(original_line) - len(stripped_line)
                indent = ' ' * indent_len
                # Add '#' right before the content starts
                lines[line_num] = f"{indent}#{stripped_line}"

        return "".join(lines)


    def action_reload(self) -> None:
        """Reload the file from disk."""
        self.load_file()
        self.notify("File reloaded.")

    def action_toggle_all(self) -> None:
        """Toggle all flags on or off."""
        if not self.flags:
            return

        enabled_count = sum(1 for f in self.flags if f.enabled)
        should_enable = enabled_count < len(self.flags) / 2

        for flag in self.flags:
            flag.enabled = should_enable

        for widget in self.flag_widgets:
            checkbox = widget.query_one(Checkbox)
            checkbox.value = should_enable

        self.update_stats()
        action = "enabled" if should_enable else "disabled"
        self.notify(f"All flags {action}.")

    def action_help(self) -> None:
        """Show help information."""
        help_text = """
Meson Config Flag Editor

Keyboard Shortcuts:
- s: Save changes
- r: Reload file
- a: Toggle all flags
- q: Quit
- Space: Toggle selected flag

Use mouse or arrow keys to navigate.
Check/uncheck boxes to enable/disable flags.
        """
        self.notify(help_text.strip())

def main():
    """Main entry point."""
    filename = sys.argv[1] if len(sys.argv) > 1 else "meson.build"
    if not os.path.exists(filename):
        print(f"Error: {filename} not found!")
        print("Usage: python vfconfig.py [path/to/meson.build]")
        sys.exit(1)
    app = MesonConfigEditor(filename)
    app.run()

if __name__ == "__main__":
    main()