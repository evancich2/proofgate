#!/usr/bin/env bash
# Proves one harness with CBMC, then confirms the proof is not vacuous.
# usage: verify/check.sh verify/<harness>.c
set -u

harness=$1
name=$(basename "$harness" .c)
root=$(cd "$(dirname "$0")/.." && pwd)
log_dir="$root/build/verify"
mkdir -p "$log_dir"

flags=$(sed -n '1s|^// cbmc: *||p' "$harness")
if [ -z "$flags" ]; then
    echo "FAIL $name: first line must be '// cbmc: <flags>'"
    exit 1
fi

# Harnesses that #include the source directly must not also link it.
sources=()
for src in "$root"/src/{http,modbus,mqtt,tlv8,api}.c; do
    base=$(basename "$src")
    grep -q "src/$base\"" "$harness" || sources+=("$src")
done

checks=(--bounds-check --pointer-check --memory-leak-check --div-by-zero-check
        --signed-overflow-check --unsigned-overflow-check --pointer-overflow-check
        --conversion-check --undefined-shift-check --unwinding-assertions)

run() { # <log> [extra flags...]
    local log=$1
    shift
    # shellcheck disable=SC2086
    cbmc "$harness" "${sources[@]}" -I "$root/include" -I "$root/verify" \
        "${checks[@]}" $flags "$@" >"$log" 2>&1
}

start=$SECONDS

run "$log_dir/$name.proof.log"
status=$?
if [ "$status" -ne 0 ]; then
    echo "FAIL $name: proof did not pass (cbmc exit $status), see build/verify/$name.proof.log"
    exit 1
fi

run "$log_dir/$name.vacuity.log" -DPG_VACUITY
vac_log="$log_dir/$name.vacuity.log"
reached=$(grep -c 'REACH: .*: FAILURE' "$vac_log")
unreached=$(grep -c 'REACH: .*: SUCCESS' "$vac_log")
if [ "$reached" -eq 0 ] || [ "$unreached" -ne 0 ]; then
    echo "FAIL $name: vacuous, $unreached of $((reached + unreached)) outcomes unreachable"
    grep 'REACH: .*: SUCCESS' "$vac_log"
    exit 1
fi

proved=$(grep -c 'PROVE: .*: SUCCESS' "$log_dir/$name.proof.log")
total=$(grep -c ': SUCCESS$' "$log_dir/$name.proof.log")
echo "PASS $name: $total properties ($proved functional), $reached outcomes reachable, $((SECONDS - start))s"
