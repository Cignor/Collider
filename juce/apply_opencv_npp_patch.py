#!/usr/bin/env python3
"""Apply CUDA 13.0 NPP compatibility patch to OpenCV's private.cuda.hpp"""

import os
import sys

def apply_patch():
    # Find the source directory from the current working directory
    # When called by FetchContent PATCH_COMMAND, we're in the opencv source directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    opencv_src = os.getcwd()  # FetchContent sets CWD to the source directory
    patch_file = os.path.join(opencv_src, "modules", "core", "include", "opencv2", "core", "private.cuda.hpp")
    
    print(f"Looking for patch file at: {patch_file}")
    print(f"Current working directory: {os.getcwd()}")
    
    if not os.path.exists(patch_file):
        print(f"Error: {patch_file} not found")
        sys.exit(1)
    
    with open(patch_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Check if already patched
    if "__CUDACC_VER_MAJOR__ >= 13" in content and "CUDA 13.0+ uses new NPP API" in content:
        print("Patch already applied")
        return
    
    # In OpenCV 4.9.0, the file includes <npp.h> and calls nppGetStream()/nppSetStream()
    # but doesn't define them. We need to add wrapper functions after the includes
    # and before they're used. The best place is right after the NPP includes and 
    # version check, before the namespace declarations.
    
    # Look for the include section and add wrappers there
    if "#include <npp.h>" in content and "nppGetStream()" in content:
        # Find where to insert - right before "namespace cv { namespace cuda {"
        insert_pos = content.find("namespace cv { namespace cuda {")
        if insert_pos > 0:
            # Go back to find a good insertion point - before the namespace, after any #endif
            # Look for a blank line or comment before namespace
            search_start = max(0, insert_pos - 100)
            before_namespace = content[search_start:insert_pos]
            
            # Find the last #endif before namespace (closing the HAVE_CUDA block)
            endif_pos = content.rfind("#endif", 0, insert_pos)
            if endif_pos > 0:
                # Find the newline after #endif
                newline_pos = content.find("\n", endif_pos)
                if newline_pos > 0:
                    insert_pos = newline_pos + 1
            
            # Check if already has our wrapper
            if insert_pos > 200:
                if "inline NppStream nppGetStream()" in content[insert_pos-200:insert_pos+100]:
                    print("NPP wrappers already present")
                    return
            
            wrapper_code = """

// CUDA 13.0+ NPP API compatibility wrappers
#if defined(__CUDACC_VER_MAJOR__) && __CUDACC_VER_MAJOR__ >= 13
// CUDA 13.0+ uses new NPP API - context-based stream management
inline NppStream nppGetStream()
{
    NppStreamContext nppStreamCtx = {0};
    nppGetStreamContext(&nppStreamCtx);
    return nppStreamCtx.hStream;
}

inline void nppSetStream(cudaStream_t stream)
{
    NppStreamContext nppStreamCtx;
    nppStreamCtx.hStream = stream;
    nppSetStreamContext(&nppStreamCtx);
}

inline NppStreamContext nppGetStreamContext()
{
    NppStreamContext nppStreamCtx = {0};
    nppGetStreamContext(&nppStreamCtx);
    return nppStreamCtx;
}
#else
// CUDA 12.x and earlier - functions come from NPP headers
// (No wrappers needed, functions exist in npp.h)
#endif // __CUDACC_VER_MAJOR__ >= 13

"""
            content = content[:insert_pos] + wrapper_code + content[insert_pos:]
            print(f"Successfully inserted NPP compatibility wrappers")
        else:
            print("ERROR: Could not find namespace cv declaration")
            sys.exit(1)
    else:
        print("ERROR: File structure different than expected")
        print(f"File contains npp.h: {'#include <npp.h>' in content}")
        print(f"File contains nppGetStream: {'nppGetStream()' in content}")
        sys.exit(1)
    
    # Write the modified content
    with open(patch_file, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)
    
    print(f"Successfully applied CUDA 13.0 NPP patch to {patch_file}")

if __name__ == "__main__":
    apply_patch()
