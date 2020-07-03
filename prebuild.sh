#! bash
set -euo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

steps=2

# step 1, clean
echo "[1/$steps - Clean]"
rm -f $DIR/*/shared-lib-*.h \
       $DIR/*/*-html.h |& sed 's/^/  /g'

# step 2, create all html assets as importable headers
# REPLACED WITH data/ directories and LittleFS toolchain
# example
# input:     foo.html
# import as: #include "foo-html.h"
# use as:    const char *fooHtml
# echo "[2/$steps - Generate HTML]"
# for html_file in $DIR/*/*.html; do
#     html_file_dir=$(dirname $html_file)
#     header_file=$(basename $html_file | sed 's/[^a-zA-Z0-9\-\_]/-/g')
#     variable_name=$(echo $header_file | sed 's/-html$/Html/' | sed 's/-//g')
#     header_guard_def_name=$(echo $header_file | sed 's/[\.\-]/_/g' | tr 'a-z' 'A-Z')_H
#     file_contents=$(cat $html_file)
#     header_file_path=$html_file_dir/$header_file.h

#     #echo "  $header_file_path <- $html_file"

#     cat > $header_file_path <<EOT
# #ifndef $header_guard_def_name
# #define $header_guard_def_name
# const char *$variable_name = R""""(
# $file_contents
# )"""";
# #endif
# EOT

# done

# step 3, copy all shared library files
#
# this was needed because vscode and arduino do not play nicely with
# relative imports and library folders (so this is the only thing i could think of)
#
# example
# input:  shared-lib/foo.h
# output: incubator/shared-lib-foo.h, coop-command/shared-lib-foo.h, ...
# import as: #include "shared-lib-foo.h"
echo "[2/$steps - Generate Shared Libs]"
for shared_lib_file in $DIR/shared-lib/*.h; do
    shared_lib_file_basename=$(basename $shared_lib_file)
    for project_dir in $DIR/*; do
        if [[ "$project_dir" =~ "shared-lib" || ! -d "$project_dir" ]]; then
            continue
        fi

        shared_lib_target=$project_dir/shared-lib-$shared_lib_file_basename

        #echo "  $shared_lib_target <- $shared_lib_file"
        cp $shared_lib_file $shared_lib_target
    done
done