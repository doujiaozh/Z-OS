PYTHON ?= python3
QEMU ?= qemu-system-x86_64
IMAGE := build/z-os.img

.PHONY: all build test smoke keyboard shell verify run clean

all: build

build:
	$(PYTHON) tools/build.py

test:
	$(PYTHON) tools/verify.py test

smoke:
	$(PYTHON) tools/verify.py smoke

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
