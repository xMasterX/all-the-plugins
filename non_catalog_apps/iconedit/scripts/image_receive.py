#!/usr/bin/env python3

import os, sys, cmd, re, shutil


def receive_text(prompt: str = "Ready to receive data!") -> list[str]:
    """Reads lines of text from stdin until we receive a blank line"""
    if prompt:
        print(prompt)
    text = []
    write_complete = False
    while not write_complete:
        line = sys.stdin.readline().strip()
        if len(line) == 0:
            write_complete = True
            continue
        text.append(line)
    return text


def valid_binary_text(text: list[str]) -> bool:
    """Validates that the text represents hex bytes"""
    return all(
        len(line) > 0
        and len(line) % 2 == 0
        and all(h in "0123456789ABCDEF" for h in line.upper())
        for line in text
    )


def save_text(text: list[str], filename: str):
    """Save text list to filename as ASCII text"""
    with open(filename, "w") as file:
        for line in text:
            file.write(line)
            file.write("\n")


def save_binary(text: list[str], filename: str):
    """Save binary bytes to filename after hex conversion"""
    with open(filename, "wb") as file:
        for line in text:
            file.write(bytes.fromhex(line))


def receive_anim_and_save(name: str):
    """
    Receives PNG frames and saves as individual files in a new directory.
    Contents of the directory are purged
    """

    # Eliminate the directory and its contents first
    shutil.rmtree(name, ignore_errors=True)
    # Re-create new directory
    os.makedirs(name, exist_ok=True)

    # 1 or more binary frames. We'll keep track of frame number and generate filenames
    frame = 0
    while True:
        text = receive_text(None)
        if len(text) == 0:
            print(f"Something went wrong at frame: {frame}")
            print("The directory may contain a partial animation")
            break
        # If we receive file text of just "frame_rate", then we're done with the frames
        if text[0] == "frame_rate":
            frame_rate = receive_text(None)
            save_text(frame_rate[0], f"{name}/frame_rate")
            break
        save_binary(text, f"{name}/A_{name}_{frame}.png")
        frame += 1
    if frame > 0:
        print(f"Saved {frame} frames into directory {name}")


class cli(cmd.Cmd):
    prompt = ">> "
    intro = (
        "Welcome to IconEdit Image Receive. Type ? or help for available commands.\n"
        "Visit https://github.com/rdefeo/iconedit for more information"
    )
    doc_header = "Available commands (type help <command> for more information)"
    empty_line_count = 0

    def help_receive(self):
        print("Receives one or more files from the IconEdit app")
        print()
        print("  receive text filename")
        print("    - Use to save .C source files, whether single-frame or animation")
        print("      This won't send the filename (you will not be prompted) thus")
        print("      it is a required argument. Consider sending .C source files")
        print("      directly to your IDE.")
        print()
        print("  recieve bin [filename]")
        print("    - Use to save binary image formats like PNG and BMX")
        print()
        print("  receive anim [dir name]")
        print("    - Use to save multiple PNG frames of an animation. A new directory")
        print("      named 'dir name' will be created in the CWD to store the files")
        print("      If the directory already exists, it will be purged of files!")
        print()
        print("* If a filename/dir name is not supplied, you will be prompted")
        print("  to send a name from the IconEdit app on the Flipper")
        print("* If you do supply a filename/dir name, then press Right to skip")
        print("  sending when you are prompted to send a filename in the app")

    def do_receive(self, line):
        args = line.split()
        filename = ""
        if len(args) > 1:
            filename = args[1]
        if len(args) == 0 or args[0] not in ["text", "bin", "anim"]:
            self.help_receive()
            return

        if not filename:
            text = receive_text("Ready to receive filename!")
            filename = text[0]
        if not filename:
            print("ERROR! No filename or dir name provided!")
            return
        if not re.match(r"^[\w\-_\.]+$", filename):
            print("ERROR! Filename contains invalid characters")
            return

        match args[0]:
            case "text":
                text = receive_text()
                if text:
                    save_text(text, filename)
            case "bin":
                text = receive_text()
                if text and valid_binary_text(text):
                    save_binary(text, filename)
                else:
                    print("Malformed binary data!")
            case "anim":
                receive_anim_and_save(filename)
            case _:
                print(f"Unsupported: {args[0]}")
                pass

    def do_rt(self, line):
        """Shortcut for 'receive text'. See: help receive"""
        self.do_receive("text " + line)

    def do_rb(self, line):
        """Shortcut for 'receive bin'. See: help receive"""
        self.do_receive("bin " + line)

    def do_ra(self, line):
        """Shortcut for 'receive anim'. See: help receive"""
        self.do_receive("anim " + line)

    def do_quit(self, line):
        """Exit"""
        return True

    def emptyline(self):
        self.empty_line_count += 1
        if self.empty_line_count == 3:
            print("Try typing 'help' to get started!")
            self.empty_line_count = 0

    def default(self, line):
        print(f"Unknown command: {line}. Type 'help' to list available commands")


if __name__ == "__main__":
    cli().cmdloop()
