CC       ?= gcc
CSTD     := -std=c11
WARN     := -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
CPPFLAGS := -Iinclude
CFLAGS   ?= -O2 -g
HARDEN   := -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE
SAN      := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all

CORE := src/http.c src/modbus.c src/mqtt.c src/tlv8.c src/api.c
HARNESSES := $(wildcard verify/*.c)
PROOFS := $(HARNESSES:verify/%.c=proof-%)

.PHONY: all test e2e verify clean $(PROOFS)

all: build/proofgate

build:
	mkdir -p build

build/proofgate: $(CORE) src/main.c $(wildcard include/proofgate/*.h) | build
	$(CC) $(CSTD) $(WARN) $(HARDEN) $(CFLAGS) $(CPPFLAGS) $(CORE) src/main.c -pie -o $@

build/test_core: $(CORE) tests/test_core.c $(wildcard include/proofgate/*.h) | build
	$(CC) $(CSTD) $(WARN) $(SAN) $(CPPFLAGS) $(CORE) tests/test_core.c -o $@

build/fake_modbus: tests/fake_modbus.c | build
	$(CC) $(CSTD) $(WARN) $(SAN) $(CPPFLAGS) tests/fake_modbus.c -o $@

test: build/test_core
	./build/test_core

e2e: build/proofgate build/fake_modbus
	./tests/e2e.sh

verify: $(PROOFS)

$(PROOFS): proof-%: verify/%.c
	@bash verify/check.sh $<

clean:
	rm -rf build
