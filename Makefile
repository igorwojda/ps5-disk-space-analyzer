# Convenience wrapper around CMake.
#
#   make toolchain  download the PS5 SDK + SDL2 libs into .toolchain (no sudo)
#   make ps5        build build-ps5/storage-analyzer.elf
#   make package    dist/storage-analyzer.elf
#   make send       send it to elfldr: make send PS5_HOST=192.168.1.50
#   make desktop    build the same server for macOS/Linux (development)
#   make run        run it locally: http://localhost:8833/
#   make clean

TOOLCHAIN_URL := https://github.com/ps5-payload-dev/pacbrew-repo/releases/latest/download/ps5-payload-dev.tar.gz
# Prefer the project-local toolchain, then a system-wide install.
ifneq ($(wildcard .toolchain/opt/ps5-payload-sdk),)
  PS5_PAYLOAD_SDK ?= $(CURDIR)/.toolchain/opt/ps5-payload-sdk
else
  PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
endif
PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifeq ($(shell uname -s),Darwin)
  # Homebrew LLVM (with the separate lld keg) works with the SDK.
  LLVM_CONFIG ?= $(shell brew --prefix llvm 2>/dev/null)/bin/llvm-config
  export LLVM_CONFIG
endif

ELF := build-ps5/storage-analyzer.elf

.PHONY: toolchain ps5 package send desktop run clean

toolchain:
	mkdir -p .toolchain
	curl -L $(TOOLCHAIN_URL) | tar xz -C .toolchain
	@echo "SDK installed in .toolchain/opt/ps5-payload-sdk"

ps5:
	$(PS5_PAYLOAD_SDK)/bin/prospero-cmake -S . -B build-ps5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=OFF
	cmake --build build-ps5 -j
	$(PS5_PAYLOAD_SDK)/bin/prospero-strip $(ELF)

package: ps5
	mkdir -p dist
	cp $(ELF) dist/storage-analyzer.elf
	@echo "dist/storage-analyzer.elf ready"

send: ps5
	socat -t 99999999 - TCP:$(PS5_HOST):$(PS5_PORT) < $(ELF)

desktop:
	cmake -S . -B build-desktop -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build build-desktop -j

# Serves the UI from web/ so edits show up on reload.
run: desktop
	./build-desktop/storage-analyzer --web web

clean:
	rm -rf build-ps5 build-desktop dist
