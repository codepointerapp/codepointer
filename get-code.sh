#!/bin/sh

set -e

clone_or_update_repo() {
    owner="$1"
    repo="$2"
    release="$3"
    target_dir="lib/$repo"

    if [ -d "$target_dir" ]; then
        printf "Updating %s" "$target_dir"
        (cd "$target_dir" && git fetch -q origin)
    else
        printf "Cloning %s (gh:%s/%s)" "$target_dir" "$owner" "$repo"
        git clone -q "https://github.com/$owner/$repo" "$target_dir"
        (
            cd "$target_dir"
            git remote add github "git@github.com:$owner/$repo"
        )
    fi
    printf "."

    printf " Updating to %s" "$release"
    (
        cd "$target_dir"
        if ! git checkout -q "$release" 2>/dev/null &&
           ! git checkout -q "origin/$release" 2>/dev/null; then
            echo "Failed to checkout $release for $repo. Falling back to master/main."
            exit 1
        fi
    )
    echo "."
}

grep 'CPMAddPackage' CMakeLists.txt | while IFS= read -r line; do
    parsed=$(echo "$line" | sed -n \
        's/.*CPMAddPackage("gh:\([^/]*\)\/\([^#]*\)#\([^"]*\)".*/\1 \2 \3/p')

    if [ -n "$parsed" ]; then
        owner=$(echo "$parsed" | awk '{print $1}')
        repo=$(echo "$parsed"  | awk '{print $2}')
        release=$(echo "$parsed" | awk '{print $3}')

        clone_or_update_repo "$owner" "$repo" "$release"
    fi
done
