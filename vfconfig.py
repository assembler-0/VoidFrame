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
        # Send message to parent about the change
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
    
    .stats {
        background: $panel;
        padding: 1;
        margin: 1;
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
        self.original_content: str = ""
        self.flag_widgets: List[ConfigFlagWidget] = []

    def compose(self) -> ComposeResult:
        yield Header()
        with Vertical():
            yield Static(f"Editing: {self.filename}", classes="stats")
            with ScrollableContainer(classes="container", id="flag_container"):
                yield Static("Loading...")
        yield Footer()

    def on_mount(self) -> None:
        """Load the meson.build file when the app starts."""
        self.load_file()

    def load_file(self) -> None:
        """Load and parse the meson.build file."""
        try:
            with open(self.filename, 'r') as f:
                self.original_content = f.read()

            self.flags = self.parse_config_flags(self.original_content)
            self.update_display()

        except FileNotFoundError:
            self.notify(f"Error: {self.filename} not found!", severity="error")
        except Exception as e:
            self.notify(f"Error loading file: {e}", severity="error")


    def parse_config_flags(self, content: str) -> List[ConfigFlag]:
        """Parse config flags from meson.build content."""
        flags = []
        lines = content.split('\n')

        # Look for vf_config_flags array
        in_config_array = False
        array_pattern = r'vf_config_flags\s*=\s*\['

        for i, line in enumerate(lines):
            if re.search(array_pattern, line):
                in_config_array = True
                continue

            if in_config_array:
                if ']' in line:
                    break

                # More flexible pattern to handle various formats
                # Handle both commented and uncommented lines
                stripped_line = line.strip()
                if not stripped_line or stripped_line.startswith('//'):
                    continue

                # Check if line contains a flag
                if "'" in stripped_line:
                    # Extract flag content between quotes
                    start_quote = stripped_line.find("'")
                    end_quote = stripped_line.find("'", start_quote + 1)

                    if start_quote != -1 and end_quote != -1:
                        flag_full = stripped_line[start_quote + 1:end_quote]

                        # Check if line is commented
                        is_commented = stripped_line.startswith('#')

                        # Extract description (everything after # at the end)
                        description = ""
                        comment_pos = stripped_line.find('#', end_quote)
                        if comment_pos != -1:
                            description = stripped_line[comment_pos + 1:].strip()

                        # Extract flag name (remove -D prefix if present)
                        flag_name = flag_full
                        if flag_name.startswith('-D'):
                            flag_name = flag_name[2:]

                        flags.append(ConfigFlag(
                            name=flag_name,
                            enabled=not is_commented,
                            description=description,
                            line_number=i
                        ))

        return flags


    def update_display(self) -> None:
        """Update the display with current flags."""
        container = self.query_one("#flag_container")

        # Clear existing content
        container.remove_children()
        self.flag_widgets.clear()

        if not self.flags:
            container.mount(Static("No config flags found in vf_config_flags array"))
            return

        # Add statistics
        enabled_count = sum(1 for f in self.flags if f.enabled)
        stats_text = f"Total flags: {len(self.flags)} | Enabled: {enabled_count} | Disabled: {len(self.flags) - enabled_count}"

        # Create and mount stats widget
        stats_widget = Static(stats_text, classes="stats", id="stats_display")
        container.mount(stats_widget)

        # Add flag widgets
        for flag in self.flags:
            widget = ConfigFlagWidget(flag)
            self.flag_widgets.append(widget)
            container.mount(widget)

        # Debug: Add a message about how many flags were found
        self.notify(f"Loaded {len(self.flags)} flags", severity="information")

    @on(ConfigFlagChanged)
    def on_config_flag_changed(self, message: ConfigFlagChanged) -> None:
        """Handle when a config flag is toggled."""
        enabled_count = sum(1 for f in self.flags if f.enabled)
        stats_text = f"Total flags: {len(self.flags)} | Enabled: {enabled_count} | Disabled: {len(self.flags) - enabled_count}"

        # Update stats display
        stats_widget = self.query(".stats").first()
        if stats_widget and hasattr(stats_widget, 'update'):
            stats_widget.update(stats_text)

    def action_save(self) -> None:
        """Save changes to the file."""
        async def _save():
            if await self.push_screen_wait(SaveConfirmScreen()):
                try:
                    new_content = self.generate_updated_content()
                    with open(self.filename, 'w') as f:
                        f.write(new_content)
                    self.notify("File saved successfully!", severity="information")
                except Exception as e:
                    self.notify(f"Error saving file: {e}", severity="error")

        self.run_worker(_save())

    def generate_updated_content(self) -> str:
        """Generate updated file content with current flag states."""
        lines = self.original_content.split('\n')

        # Create a mapping of line numbers to flags
        line_to_flag = {flag.line_number: flag for flag in self.flags}

        for line_num, flag in line_to_flag.items():
            if line_num < len(lines):
                line = lines[line_num]

                # Parse the current line
                flag_pattern = r'^\s*(#?)\s*(\'[^\']+\',?)\s*(#.*)?$'
                match = re.match(flag_pattern, line)

                if match:
                    current_comment = match.group(1) or ""
                    flag_part = match.group(2)
                    desc_comment = match.group(3) or ""

                    # Update based on flag state
                    if flag.enabled:
                        # Remove comment if present
                        new_line = f"    {flag_part}"
                    else:
                        # Add comment if not present
                        new_line = f"#    {flag_part}"

                    # Preserve description comment
                    if desc_comment:
                        new_line += f" {desc_comment}"

                    lines[line_num] = new_line

        return '\n'.join(lines)

    def action_reload(self) -> None:
        """Reload the file from disk."""
        self.load_file()
        self.notify("File reloaded", severity="information")

    def action_toggle_all(self) -> None:
        """Toggle all flags on or off."""
        if not self.flags:
            return

        # Determine if we should enable all or disable all
        enabled_count = sum(1 for f in self.flags if f.enabled)
        should_enable = enabled_count < len(self.flags) / 2

        # Update all flags
        for flag in self.flags:
            flag.enabled = should_enable

        # Update all checkboxes
        for widget in self.flag_widgets:
            checkbox = widget.query_one(Checkbox)
            checkbox.value = should_enable

        self.update_display()
        action = "enabled" if should_enable else "disabled"
        self.notify(f"All flags {action}", severity="information")

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
        self.notify(help_text.strip(), severity="information")

def main():
    """Main entry point."""
    filename = sys.argv[1] if len(sys.argv) > 1 else "meson.build"

    if not os.path.exists(filename):
        print(f"Error: {filename} not found!")
        print("Usage: python meson_config_editor.py [meson.build]")
        sys.exit(1)

    app = MesonConfigEditor(filename)
    app.run()

if __name__ == "__main__":
    main()