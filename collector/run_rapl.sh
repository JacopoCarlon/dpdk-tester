#!/bin/bash
# run_rapl.sh - Power measurement tool with RAPL

# Default values
DEFAULT_ITERATIONS=50
DEFAULT_INTERVAL=1
AUTO_CONFIRM=false
REMOVE_TMP=false

# Helper functions
show_usage() {
    echo "Usage: $0 [-y] [-r] [-t tmpfile] [-c iterations] [-s interval] outputfile"
    echo "Options:"
    echo "  -y          Auto-confirm overwrites"
    echo "  -r          Remove temporary file after completion"
    echo "  -t tmpfile  Temporary data file (default: data_{outputfile})"
    echo "  -c count    Number of measurements (default: $DEFAULT_ITERATIONS)"
    echo "  -s seconds  Sampling interval in seconds (default: $DEFAULT_INTERVAL)"
    echo "Examples:"
    echo "  $0 results.log                   # Basic usage with defaults"
    echo "  $0 -y -r results.log             # Auto-overwrite and clean up"
    echo "  $0 -c 100 -s 0.5 results.log     # 100 samples at 500ms interval"
    echo "  $0 -t custom.tmp results.log     # Custom temporary file"
}

# Parse arguments
while getopts ":yrt:c:s:" opt; do
    case $opt in
        y) AUTO_CONFIRM=true ;;
        r) REMOVE_TMP=true ;;
        t) TMP_FILE="$OPTARG" ;;
        c) ITERATIONS="$OPTARG" ;;
        s) INTERVAL="$OPTARG" ;;
        \?) echo "Invalid option: -$OPTARG" >&2; show_usage; exit 1 ;;
        :) echo "Option -$OPTARG requires an argument" >&2; exit 1 ;;
    esac
done
shift $((OPTIND-1))

# Validate required output file
if [ $# -lt 1 ]; then
    echo "Error: Output file required" >&2
    show_usage
    exit 1
fi
OUTPUT_FILE="$1"

# Set default temporary file name if not specified
if [ -z "${TMP_FILE}" ]; then
    OUTPUT_DIR=$(dirname "${OUTPUT_FILE}")
    OUTPUT_BASE=$(basename "${OUTPUT_FILE}")
    TMP_FILE="${OUTPUT_DIR}/data_${OUTPUT_BASE}"
fi

# Set defaults for unprovided parameters
ITERATIONS=${ITERATIONS:-$DEFAULT_ITERATIONS}
INTERVAL=${INTERVAL:-$DEFAULT_INTERVAL}

# File existence checks
check_overwrite() {
    local file="$1"
    local description="$2"
    
    if [ -f "$file" ]; then
        if $AUTO_CONFIRM; then
            echo "Auto-overwriting existing $description: $file"
            rm -f "$file"
            return
        fi
        
        read -p "$description '$file' exists. Overwrite? [y/N] " answer
        case $answer in
            [Yy]*) rm -f "$file" ;;
            *) echo "Aborted"; exit 1 ;;
        esac
    fi
}

# Check output file
check_overwrite "$OUTPUT_FILE" "Output file"

# Check temporary file
check_overwrite "$TMP_FILE" "Temporary file"

# Show configuration
echo "=== Power Measurement Configuration ==="
echo "Output file:    $OUTPUT_FILE"
echo "Temporary file: $TMP_FILE"
echo "Iterations:     $ITERATIONS"
echo "Interval:       ${INTERVAL}s"
echo "Remove temp:    $([ $REMOVE_TMP = true ] && echo "Yes" || echo "No")"
echo "======================================="

# Run data collection
echo "Starting data collection..."
sudo ./rapl_logger.sh "$ITERATIONS" "$TMP_FILE" "$INTERVAL"

# Process results
echo "Calculating averages..."
python3 avg_power.py -i "$TMP_FILE" -o "$OUTPUT_FILE"

# Cleanup temporary file if requested
if $REMOVE_TMP; then
    echo "Removing temporary file: $TMP_FILE"
    rm -f "$TMP_FILE"
fi

echo "Measurement complete. Results saved to $OUTPUT_FILE"

