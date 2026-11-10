#!/usr/bin/env bash
# Runs the real server against a fake Modbus device and checks the HTTP surface.
set -u

HTTP_PORT=${HTTP_PORT:-18080}
MB_PORT=${MB_PORT:-15020}
BASE="http://127.0.0.1:$HTTP_PORT"

./build/fake_modbus "$MB_PORT" &
fake=$!
./build/proofgate --port "$HTTP_PORT" --modbus "127.0.0.1:$MB_PORT" 2>/dev/null &
server=$!
trap 'kill $fake $server 2>/dev/null; wait 2>/dev/null' EXIT

for _ in $(seq 1 50); do
    curl -s -o /dev/null "$BASE/healthz" && break
    sleep 0.1
done

failed=0
expect() { # <name> <want-status> <want-body-or-empty> <curl args...>
    local name=$1 want_status=$2 want_body=$3
    shift 3
    local out body status
    out=$(curl -s -w '\n%{http_code}' "$@")
    status=${out##*$'\n'}
    body=${out%$'\n'*}
    if [ "$status" != "$want_status" ] || { [ -n "$want_body" ] && [ "$body" != "$want_body" ]; }; then
        echo "FAIL $name: got $status '$body'"
        failed=$((failed + 1))
    else
        echo "ok   $name"
    fi
}

expect health 200 '{"status":"ok"}' "$BASE/healthz"
expect read_one 200 '{"unit":1,"table":"holding","address":10,"values":[30]}' \
    "$BASE/api/v1/modbus/1/holding/10"
expect read_many 200 '{"unit":1,"table":"input","address":2,"values":[6,9,12]}' \
    "$BASE/api/v1/modbus/1/input/2?count=3"
expect read_max 200 '' "$BASE/api/v1/modbus/1/holding/0?count=125"
expect exception 502 '{"error":"modbus_exception","code":2}' "$BASE/api/v1/modbus/1/holding/1000"
expect timeout 504 '{"error":"upstream_timeout"}' "$BASE/api/v1/modbus/99/holding/0"
expect bad_unit 400 '{"error":"bad_request"}' "$BASE/api/v1/modbus/999/holding/0"
expect not_found 404 '{"error":"not_found"}' "$BASE/nope"
expect post_refused 405 '{"error":"method"}' -X POST "$BASE/healthz"
expect body_refused 413 '{"error":"body_not_accepted"}' -d 'x' "$BASE/healthz"
expect chunked_refused 501 '{"error":"transfer_encoding"}' \
    -H 'Transfer-Encoding: chunked' -d 'x' "$BASE/healthz"

# Two pipelined requests on one connection, sent with bash's /dev/tcp.
exec 3<>"/dev/tcp/127.0.0.1/$HTTP_PORT"
printf 'GET /healthz HTTP/1.1\r\nHost: a\r\n\r\nGET /healthz HTTP/1.1\r\nHost: a\r\nConnection: close\r\n\r\n' >&3
count=$(timeout 3 cat <&3 | grep -c 'HTTP/1.1 200 OK')
exec 3>&-
if [ "$count" = 2 ]; then echo "ok   pipelined"; else echo "FAIL pipelined: $count"; failed=$((failed + 1)); fi

# An upstream that is down must surface as a gateway error, not hang or crash.
kill $fake 2>/dev/null
wait $fake 2>/dev/null
expect upstream_down 502 '{"error":"upstream_failed"}' "$BASE/api/v1/modbus/1/holding/0"
expect still_alive 200 '{"status":"ok"}' "$BASE/healthz"

echo "$failed failed"
exit "$failed"
