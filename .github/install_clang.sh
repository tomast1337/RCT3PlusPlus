#!/bin/sh
# Installs Clang in the CI and release jobs, which run in the gcc:16 image (Debian trixie). Clang 23 comes from
# apt.llvm.org (it keeps only the latest point release of each major, so the major is what is pinned). GCC 16 stays
# for its libstdc++ only: std::optional<T&> and std::inplace_vector need libstdc++ 16, and Debian's own GCC 14 in
# the same image would be picked otherwise. Puts Clang (and ld.lld) first on the job's PATH.
set -eu
version=23
apt-get update -qq
apt-get install -y -qq --no-install-recommends ca-certificates curl
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key > /etc/apt/trusted.gpg.d/apt.llvm.org.asc
echo "deb http://apt.llvm.org/trixie/ llvm-toolchain-trixie-$version main" > /etc/apt/sources.list.d/llvm.list
apt-get update -qq
apt-get install -y -qq --no-install-recommends "clang-$version" "lld-$version" "libclang-rt-$version-dev"
# Clang reads clang.cfg and clang++.cfg next to its binary on every run.
bin=/usr/lib/llvm-$version/bin
gcc_dir=$(dirname "$(g++ -print-libgcc-file-name)")
for driver in clang clang++; do
    echo "--gcc-install-dir=$gcc_dir" > "$bin/$driver.cfg"
done
if [ -n "${GITHUB_PATH:-}" ]; then
    echo "$bin" >> "$GITHUB_PATH"
fi
"$bin/clang++" --version
