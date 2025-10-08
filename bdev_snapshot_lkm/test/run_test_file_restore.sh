#!/bin/bash

# Explanation:
# The purpose of this test is as follows:
# - After the device-file has been modified, the two files are expected to be different.
# - After performing the snapshot restore, the two files should be identical.
# This comparison allows us to verify that the restore functionality works correctly.

# Paths to the files to compare
ORIGINAL_FILE="./original_image/image"
RESTORED_FILE="./SINGLEFILE-FS/image"

# Path to the comparison program
COMPARE_PROG="./file_compare"

# Check if the comparison program exists
if [ ! -x "$COMPARE_PROG" ]; then
    echo "Error: Comparison program '$COMPARE_PROG' not found or not executable."
    exit 1
fi

# Check if the files exist
if [ ! -f "$ORIGINAL_FILE" ]; then
    echo "Error: Original file '$ORIGINAL_FILE' not found."
    exit 1
fi

if [ ! -f "$RESTORED_FILE" ]; then
    echo "Error: Restored file '$RESTORED_FILE' not found."
    exit 1
fi

# Run the comparison
echo "Comparing files..."
$COMPARE_PROG "$ORIGINAL_FILE" "$RESTORED_FILE"

