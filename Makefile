PYTHON ?= python
QEMU ?= qemu-system-x86_64
IMAGE := build/z-os.img
ARCH ?= x86_64

.PHONY: all build build-riscv test smoke bootcheck storage gui keyboard shell verify run run-riscv clean

all: build

build:
	$(PYTHON) tools/build.py build --arch $(ARCH)

build-riscv:
	$(PYTHON) tools/build.py build --arch riscv

minidb:
	$(PYTHON) tools/verify.py minidb

test:
	$(PYTHON) tools/verify.py test

smoke:
	$(PYTHON) tools/verify.py smoke

bootcheck:
	$(PYTHON) tools/verify.py bootcheck

storage:
	$(PYTHON) tools/verify.py storage

gui:
	$(PYTHON) tools/verify.py gui

keyboard:
	$(PYTHON) tools/verify.py keyboard

shell:
	$(PYTHON) tools/verify.py shell

verify:
	$(PYTHON) tools/verify.py all

run: build
	$(QEMU) -m 256M -drive format=raw,file=$(IMAGE),if=floppy -boot a

run-riscv: build-riscv
	$(PYTHON) tools/verify_riscv.py

clean:
	$(PYTHON) tools/build.py clean
