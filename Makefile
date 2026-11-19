# Thin wrapper over CMake and the lab scripts.
PRESET ?= debug
.PHONY: build test test-asan check lab lab-kvm lab-run lab-clean e2e e2e-kvm clean

build:
	cmake --preset $(PRESET) && cmake --build --preset $(PRESET) -j

test: build
	ctest --preset $(PRESET)
	@if [ -x scripts/check-fastpath.sh ]; then scripts/check-fastpath.sh; fi

test-asan:
	cmake --preset asan && cmake --build --preset asan -j && ctest --preset asan

check:
	scripts/check.sh

lab:
	lab/lab.sh up

lab-kvm:
	lab/lab.sh up --kvm

lab-run:
	lab/lab.sh run

lab-clean:
	lab/lab.sh down

e2e:
	tests/e2e/run.sh

e2e-kvm:
	tests/e2e/run.sh --kvm

clean:
	rm -rf build
