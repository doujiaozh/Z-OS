PYTHON ?= python3
QEMU ?= qemu-system-x86_64
IMAGE := build/z-os.img

.PHONY: all build test smoke bootcheck storage gui keyboard shell verify run clean minidb

all: build

build:
	$(PYTHON) tools/build.py

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

clean:
	$(PYTHON) tools/build.py clean
