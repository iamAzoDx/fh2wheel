PYTHON ?= python3
HOST_CC ?= gcc
WINDOWS_CC ?= x86_64-w64-mingw32-gcc
WINDOWS_CFLAGS ?= -O2 -Wall -Wextra -Werror
HOST_CFLAGS ?= -O2 -Wall -Wextra -Werror
BUILD_DIR := build
GENERATED_DIR := $(BUILD_DIR)/generated
DLL_SOURCES := $(wildcard dll/src/*.c)
DLL_HEADERS := $(wildcard dll/src/*.h)
DLL_TARGET := $(BUILD_DIR)/fh2wheel.dll
PROXY_LIBRARY := D3DCOMPILER_47.dll
PROXY_EXPORTS := proxy/d3dcompiler_47_exports.txt
PROXY_THUNKS := $(GENERATED_DIR)/d3dcompiler_thunks.c
PROXY_DEF := $(GENERATED_DIR)/d3dcompiler.def
PROXY_SOURCES := proxy/src/d3dcompiler_proxy.c dll/src/log.c
PROXY_TARGET := $(BUILD_DIR)/$(PROXY_LIBRARY)
PROBE_TARGET := $(BUILD_DIR)/fh2wheel-probe.exe
UI_SOURCES := ui/src/config_ui.c ui/src/ui_text.c ui/src/device_lists.c ui/src/preview.c ui/src/preview_model.c dll/src/config.c dll/src/log.c dll/src/input_mapping.c \
	dll/src/dinput_state.c
UI_HEADERS := $(wildcard ui/src/*.h)
UI_TARGET := $(BUILD_DIR)/fh2wheel-config.exe
VERSION_MAJOR := $(shell sed -n 's/^\#define FHW_VERSION_MAJOR \([0-9]*\)u$$/\1/p' dll/src/fh2wheel.c)
VERSION_MINOR := $(shell sed -n 's/^\#define FHW_VERSION_MINOR \([0-9]*\)u$$/\1/p' dll/src/fh2wheel.c)
VERSION := $(VERSION_MAJOR).$(VERSION_MINOR)
RELEASE_ZIP := $(BUILD_DIR)/fh2wheel-v$(VERSION).zip
PUBLIC_DIR := $(if $(wildcard public/README.md),public,.)
PUBLIC_EXPORT_DIR ?= ../fh2-wheel-public
RELEASE_FILES := $(DLL_TARGET) $(PROXY_TARGET) $(UI_TARGET) $(PROBE_TARGET) $(PUBLIC_DIR)/README.md \
	$(PUBLIC_DIR)/LICENSE $(PUBLIC_DIR)/CREDITS.md $(PUBLIC_DIR)/THIRD-PARTY-NOTICES.md
VM_TEST_TARGET := $(BUILD_DIR)/test_ffb_vm
MAPPING_TEST_TARGET := $(BUILD_DIR)/test_input_mapping
UI_TEXT_TEST_TARGET := $(BUILD_DIR)/test_ui_text
DEVICE_LISTS_TEST_TARGET := $(BUILD_DIR)/test_device_lists
PREVIEW_TEST_TARGET := $(BUILD_DIR)/test_preview_model
CONFIG_TEST_TARGET := $(BUILD_DIR)/test_config.exe
WINDOWS_TEST_SCRIPT := scripts/test-windows.ps1
PYTHON_SOURCES := $(wildcard tools/*.py tests/unit/*.py)

.PHONY: build lint test windows-test check release public clean hooks

build:
	@if command -v $(WINDOWS_CC) >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory $(DLL_TARGET) $(PROXY_TARGET) $(PROBE_TARGET) $(UI_TARGET) \
			$(CONFIG_TEST_TARGET); \
	else \
		echo "build: SKIPPED -- $(WINDOWS_CC) not found (sudo apt install mingw-w64)"; \
	fi

$(DLL_TARGET): $(DLL_SOURCES) $(DLL_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(WINDOWS_CC) $(WINDOWS_CFLAGS) -shared -o $@ $(DLL_SOURCES) -ldinput8 -ldxguid -lwinmm

$(PROXY_THUNKS) $(PROXY_DEF): $(PROXY_EXPORTS) tools/gen_proxy.py
	@mkdir -p $(GENERATED_DIR)
	$(PYTHON) tools/gen_proxy.py $(PROXY_LIBRARY) $(PROXY_EXPORTS) $(PROXY_THUNKS) $(PROXY_DEF)

$(PROXY_TARGET): $(PROXY_SOURCES) $(PROXY_THUNKS) $(PROXY_DEF) $(DLL_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(WINDOWS_CC) $(WINDOWS_CFLAGS) -I dll/src -shared -o $@ $(PROXY_SOURCES) $(PROXY_THUNKS) $(PROXY_DEF)

$(PROBE_TARGET): probe/src/probe.c
	@mkdir -p $(BUILD_DIR)
	$(WINDOWS_CC) $(WINDOWS_CFLAGS) -o $@ $< -ldinput8 -ldxguid

$(UI_TARGET): $(UI_SOURCES) $(DLL_HEADERS) $(UI_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(WINDOWS_CC) $(WINDOWS_CFLAGS) -municode -mwindows -I dll/src -o $@ $(UI_SOURCES) -ldinput8 -ldxguid -lcomctl32 -lgdi32 -ldwmapi

$(VM_TEST_TARGET): tests/unit/test_ffb_vm.c dll/src/ffb_vm.c dll/src/ffb_vm.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -I dll/src -o $@ tests/unit/test_ffb_vm.c dll/src/ffb_vm.c -lm

$(MAPPING_TEST_TARGET): tests/unit/test_input_mapping.c dll/src/input_mapping.c dll/src/input_mapping.h dll/src/config.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -I dll/src -o $@ tests/unit/test_input_mapping.c dll/src/input_mapping.c -lm

$(UI_TEXT_TEST_TARGET): tests/unit/test_ui_text.c ui/src/ui_text.c $(UI_HEADERS) dll/src/config.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -I dll/src -I ui/src -o $@ tests/unit/test_ui_text.c ui/src/ui_text.c

$(DEVICE_LISTS_TEST_TARGET): tests/unit/test_device_lists.c ui/src/device_lists.c ui/src/device_lists.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -I ui/src -o $@ tests/unit/test_device_lists.c ui/src/device_lists.c

$(PREVIEW_TEST_TARGET): tests/unit/test_preview_model.c ui/src/preview_model.c ui/src/preview_model.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -I ui/src -o $@ tests/unit/test_preview_model.c ui/src/preview_model.c -lm

$(CONFIG_TEST_TARGET): tests/windows/test_config.c dll/src/config.c dll/src/log.c $(DLL_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(WINDOWS_CC) $(WINDOWS_CFLAGS) -I dll/src -o $@ tests/windows/test_config.c dll/src/config.c dll/src/log.c

lint:
	$(PYTHON) -m py_compile $(PYTHON_SOURCES)

test: $(VM_TEST_TARGET) $(MAPPING_TEST_TARGET) $(UI_TEXT_TEST_TARGET) $(DEVICE_LISTS_TEST_TARGET) $(PREVIEW_TEST_TARGET)
	./$(VM_TEST_TARGET)
	./$(MAPPING_TEST_TARGET)
	./$(UI_TEXT_TEST_TARGET)
	./$(DEVICE_LISTS_TEST_TARGET)
	./$(PREVIEW_TEST_TARGET)
	$(PYTHON) -m unittest discover -s tests/unit -p 'test_*.py' -v

windows-test: build
	@if command -v powershell.exe >/dev/null 2>&1 && powershell.exe -NoProfile -Command exit 0 >/dev/null 2>&1; then \
		powershell.exe -NoProfile -ExecutionPolicy Bypass -File $(WINDOWS_TEST_SCRIPT); \
	else \
		echo "windows-test: SKIPPED -- Windows not reachable from here; run $(WINDOWS_TEST_SCRIPT) on Windows"; \
	fi

check: build lint test windows-test

release: check
	$(PYTHON) tools/make_release.py $(RELEASE_ZIP) $(RELEASE_FILES)

public: check
	$(PYTHON) tools/export_public.py . $(PUBLIC_EXPORT_DIR)

hooks:
	git config core.hooksPath scripts/hooks

clean:
	rm -rf $(BUILD_DIR)
