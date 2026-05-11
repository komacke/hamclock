#!/bin/bash

# the paths below are relative to the directory where the generate script is located
script_path=${0%/*}/
src_path=${script_path}../../ESPHamClock/
doc_eeprompath="${script_path}../../doc/eeprom/"
contrib_path="${script_path}../../hamclock-contrib/"
eeprom_file="$HOME/.hamclock/eeprom"

usage() {
    echo "Usage: $0 [-s src_path] [-c contrib_path] [-e doc_eeprompath]"
    echo "  -s  path to ESPHamClock source    (default: ${src_path})"
    echo "  -c  path to hamclock-contrib      (default: ${contrib_path})"
    echo "  -e  path to output eeprom doc dir (default: ${doc_eeprompath})"
    echo "  -f  path to eeprom file           (default: ${eeprom_file})"
    exit 1
}

while getopts "s:c:e:f:" opt; do
    case $opt in
        s)  src_path="${OPTARG}"
            # ensure trailing slash
            src_path="${src_path%/}/"
            if [[ ! -f "${src_path}nvram.cpp" ]]; then
                echo "Error: -s invalid path, ${src_path}nvram.cpp does not exist"
                exit 1
            fi
            ;;
        c)  contrib_path="${OPTARG}"
            contrib_path="${contrib_path%/}/"
            if [[ ! -f "${contrib_path}hceeprom.pl" ]]; then
                echo "Error: -c invalid path, ${contrib_path}hceeprom.pl does not exist"
                exit 1
            fi
            ;;
        e)  doc_eeprompath="${OPTARG}"
            doc_eeprompath="${doc_eeprompath%/}/"
            if [[ ! -w "${doc_eeprompath}hceeprom.log" ]] && \
               ! touch "${doc_eeprompath}hceeprom.log" 2>/dev/null; then
                echo "Error: -e invalid path, ${doc_eeprompath}hceeprom.log cannot be written"
                exit 1
            fi
            ;;
        f)  eeprom_file="${OPTARG}"
            if [[ ! -f "${eeprom_file}" ]]; then
                echo "Error: -f invalid file, ${eeprom_file} does not exist"
                exit 1
            fi
            ;;
        *)  usage
            ;;
    esac
done
shift $((OPTIND-1))

# validate defaults if not overridden by args
if [[ ! -f "${src_path}nvram.cpp" ]]; then
    echo "Error: default src_path invalid, ${src_path}nvram.cpp does not exist"
    exit 1
fi
if [[ ! -f "${contrib_path}hceeprom.pl" ]]; then
    echo "Error: default contrib_path invalid, ${contrib_path}hceeprom.pl does not exist"
    exit 1
fi
csv_file=${doc_eeprompath}hceeprom.csv
log_file=${doc_eeprompath}hceeprom.log

if [[ ! -w "$log_file" ]] && \
   ! touch "$log_file" 2>/dev/null; then
    echo "Error: default doc_eeprompath invalid, $log_file cannot be written"
    exit 1
fi

src_path=$(realpath "$src_path")/
contrib_path=$(realpath "$contrib_path")/
log_file=$(realpath "$log_file")
csv_file=$(realpath "$csv_file")

cd ${src_path}
perl ${contrib_path}hceeprom.pl -a -e ${eeprom_file} | grep NV_ > ${log_file}
echo Addr,Name,Len,Type,Description > ${csv_file}
awk '
BEGIN { FS=" " ; OFS="," }
{
    # Extract first 4 fields
    f1 = $1
    f2 = $2
    f3 = $3
    f4 = $4

    # Rebuild field 5 from field 5 to end
    f5 = ""
    for (i=5; i<=NF; i++) {
        if (f5 != "") f5 = f5 " "
        f5 = f5 $i
    }

    # Properly quote field 5 if needed
    if (index(f5, ",") || index(f5, "\"") || index(f5, "\n")) {
        gsub(/"/, "\"\"", f5)
        f5 = "\"" f5 "\""
    }

    print f1, f2, f3, f4, f5
}' ${log_file} >> ${csv_file}
rm ${log_file}
