#!/bin/bash
set -e

compile() {
    # sometimes the compilation fails, attempt to compile multiple times
    local max_attempts=10
    local attempt=1
    
    while [ $attempt -le $max_attempts ]; do
        if pio run; then
            echo "Compilation successful on attempt $attempt"
            return 0
        else
            echo "Compilation failed on attempt $attempt"
            if [ $attempt -eq $max_attempts ]; then
                echo "Max attempts reached. Compilation failed."
                return 1
            fi
            echo "Retrying..."
            ((attempt++))
        fi
    done
}



# Copy the contents of the hosts mapped directory to the container
mkdir -p /js/src
cp -r /mapped/* /js/src


include_deps() {
    # If there is an ino file in the src directory, then rename it to main.cpp. Add
    # a special case if there already is a main.cpp file, in this case we will
    # rename it to main2.hpp, then generate the main.cpp file and then include the main2.hpp
    # file in the main.cpp file.
    if [ -f /js/src/*.ino ]; then
        # Check if main.cpp already exists
        if [ -f /js/src/main.cpp ]; then
            # special case, main.cpp exists, so we want to rename it to main2.hpp
            mv /js/src/main.cpp /js/src/main2.hpp
        fi
        mv /js/src/*.ino /js/src/generated_main.cpp
        # If main2.hpp exists (because it was renamed, then append it to main.cpp)
        if [ -f /js/src/main2.hpp ]; then
            # the main2.hpp file was created, so include it.
            echo '#include "main2.hpp"' >> /js/src/generated_main.cpp
        fi
    fi
}

# Remove the .pio directory copy, if it exists because this could contain build
# artifacts from a previous build
cd /js


# We only want to run this compile pre-step once. Leave a breadcrumb to indicate
# that we have already done this.

include_deps

#############################################
compile
#############################################


# Ensure the directory exists
mkdir -p /mapped/fastled_js


# Copy (overwrite if exists) the files
cp -f ./.pio/build/*/fastled.js /mapped/fastled_js/fastled.js
cp -f ./.pio/build/*/fastled.wasm /mapped/fastled_js/fastled.wasm
cp -f ./index.html /mapped/fastled_js/index.html

# now open the files and write them a second time just to be safe
cat ./.pio/build/*/fastled.js > /mapped/fastled_js/fastled.js
cat ./.pio/build/*/fastled.wasm > /mapped/fastled_js/fastled.wasm
cat ./index.html > /mapped/fastled_js/index.html

# now clean up the files
rm -rf /js/src/*
