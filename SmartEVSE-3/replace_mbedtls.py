Import("env")
import os
import shutil
import hashlib

# This script replaces the default mbedtls libraries with custom versions
# that have error strings removed and max context size of 6Kb to reduce RAM and flash usage.

def file_hash(filepath):
    """Calculate MD5 hash of a file."""
    h = hashlib.md5()
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            h.update(chunk)
    return h.hexdigest()

def files_identical(file1, file2):
    """Check if two files are identical by comparing their hashes."""
    if not os.path.exists(file1) or not os.path.exists(file2):
        return False
    # Quick size check first
    if os.path.getsize(file1) != os.path.getsize(file2):
        return False
    # Full hash comparison
    return file_hash(file1) == file_hash(file2)

def replace_mbedtls_libs(env):
    """Copy custom mbedtls libraries to the framework directory before linking."""
    
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    
    # Determine the correct SDK path based on the board
    board = env.get("BOARD", "esp32dev")
    if "s3" in board.lower():
        sdk_lib_path = os.path.join(framework_dir, "tools", "sdk", "esp32s3", "lib")
    else:
        sdk_lib_path = os.path.join(framework_dir, "tools", "sdk", "esp32", "lib")
    
    # Custom libraries location in project
    project_dir = env.get("PROJECT_DIR")
    if "s3" in board.lower():
        custom_lib_path = os.path.join(project_dir, "custom_libs", "esp32s3")
    else:
        custom_lib_path = os.path.join(project_dir, "custom_libs", "esp32")
    
    libs_to_replace = [
        "libmbedcrypto.a",
        "libmbedtls.a",
        "libmbedtls_2.a",
        "libmbedx509.a"
    ]
    
    if not os.path.exists(custom_lib_path):
        print(f"Warning: Custom mbedtls library path not found: {custom_lib_path}")
        return
    
    for lib in libs_to_replace:
        src = os.path.join(custom_lib_path, lib)
        dst = os.path.join(sdk_lib_path, lib)
        
        if os.path.exists(src):
            # Create backup if it doesn't exist
            backup = dst + ".original"
            if not os.path.exists(backup):
                if os.path.exists(dst):
                    shutil.copy2(dst, backup)
                    print(f"Created backup: {backup}")
            
            # Only copy if files differ
            if files_identical(src, dst):
                print(f"{lib} already up to date")
            else:
                shutil.copy2(src, dst)
                print(f"Replaced {lib} with custom version")
        else:
            print(f"Warning: Custom library not found: {src}")

# Run immediately when script is loaded (before build starts)
replace_mbedtls_libs(env)
