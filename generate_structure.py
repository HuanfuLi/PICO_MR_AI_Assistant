#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os

# --- Configuration ---

# Add any directories or files you want to ignore to this set.
# Common examples include virtual environments, build artifacts, and cache folders.
IGNORE_LIST = {
    '.git',
    '.vscode',
    '__pycache__',
    '.idea',
    'node_modules',
    'build',
    'dist',
    '.DS_Store',
    'venv',
    '.env',
    'generate_structure.py',  # Exclude the script itself.
    'project_structure.txt'   # Exclude the output file.
}

# The name of the file where the project structure will be saved.
OUTPUT_FILENAME = "project_structure.txt"

# --- Script Logic ---

def generate_tree(root_dir):
    """
    Generates a list of strings representing the project's directory tree.
    """
    tree_lines = []
    
    # Get the project's root folder name.
    project_name = os.path.basename(os.path.abspath(root_dir))
    tree_lines.append(f"{project_name}/")

    def walk_dir(current_dir, prefix=""):
        """
        A recursive function to walk through the directory and build the tree structure.
        """
        # Get all items in the current directory, filter out ignored ones, and sort them.
        try:
            items = sorted([item for item in os.listdir(current_dir) if item not in IGNORE_LIST])
        except FileNotFoundError:
            # Skip if a directory doesn't exist (e.g., a broken symbolic link).
            return

        # Use different prefixes for items to create the tree structure.
        pointers = ["├── "] * (len(items) - 1) + ["└── "]

        for pointer, item in zip(pointers, items):
            path = os.path.join(current_dir, item)
            tree_lines.append(f"{prefix}{pointer}{item}")

            if os.path.isdir(path):
                # If the item is a directory, recurse into it with an updated prefix.
                extension = "│   " if pointer == "├── " else "    "
                walk_dir(path, prefix + extension)

    walk_dir(root_dir)
    return "\n".join(tree_lines)

def main():
    """
    Main function to run the script.
    """
    # The script assumes it's located in the root of the project to be scanned.
    project_root = os.getcwd() 
    
    print(f"🔍  Scanning project structure for '{os.path.basename(project_root)}'...")
    
    try:
        project_tree = generate_tree(project_root)
        
        # Write the generated tree to the output file.
        with open(OUTPUT_FILENAME, "w", encoding="utf-8") as f:
            f.write(project_tree)
            
        print(f"✅  Success! Project structure saved to '{OUTPUT_FILENAME}'")
        print("\n--- File Preview ---")
        print(project_tree)
        print("--------------------")

    except Exception as e:
        print(f"❌  An error occurred: {e}")

if __name__ == "__main__":
    main()