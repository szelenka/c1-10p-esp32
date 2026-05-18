# ============================================================================
# Firmware build / flash / monitor
# ============================================================================

FIRMWARE_ENV ?= esp32dev
UPLOAD_PORT ?=
PARTITIONS_FILE ?= partitions.csv
NVS_OFFSET ?= $(shell awk -F',' '/^nvs[[:space:]]*,/ {gsub(/[[:space:]]*/, "", $$4); print $$4; exit}' $(PARTITIONS_FILE))
NVS_SIZE ?= $(shell awk -F',' '/^nvs[[:space:]]*,/ {gsub(/[[:space:]]*/, "", $$5); print $$5; exit}' $(PARTITIONS_FILE))

define run_firmware_build
	if [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Using PlatformIO local wrapper (env=$(FIRMWARE_ENV))"; \
		./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)"; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Using PlatformIO global CLI (env=$(FIRMWARE_ENV))"; \
		pio run -e "$(FIRMWARE_ENV)"; \
	elif command -v idf.py >/dev/null 2>&1; then \
		echo "Using ESP-IDF idf.py build"; \
		idf.py build; \
	else \
		echo "Error: no supported firmware tool found (expected ./.scripts/pio_local.sh, pio, or idf.py)." >&2; \
		exit 1; \
	fi
endef

define run_firmware_flash
	if [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Using PlatformIO local wrapper flash/monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t upload; \
		else \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" -t upload; \
		fi; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Using PlatformIO global CLI flash/monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			pio run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t upload; \
		else \
			pio run -e "$(FIRMWARE_ENV)" -t upload; \
		fi; \
	elif command -v idf.py >/dev/null 2>&1; then \
		echo "Using ESP-IDF idf.py flash monitor"; \
		idf.py flash monitor; \
	else \
		echo "Error: no supported firmware tool found (expected ./.scripts/pio_local.sh, pio, or idf.py)." >&2; \
		exit 1; \
	fi
endef

define run_firmware_monitor
	if [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Using PlatformIO local wrapper monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t monitor; \
		else \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" -t monitor; \
		fi; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Using PlatformIO global CLI monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			pio run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t monitor; \
		else \
			pio run -e "$(FIRMWARE_ENV)" -t monitor; \
		fi; \
	elif command -v idf.py >/dev/null 2>&1; then \
		echo "Using ESP-IDF idf.py monitor"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			idf.py -p "$(UPLOAD_PORT)" monitor; \
		else \
			idf.py monitor; \
		fi; \
	else \
		echo "Error: no supported firmware tool found (expected ./.scripts/pio_local.sh, pio, or idf.py)." >&2; \
		exit 1; \
	fi
endef

define run_firmware_erase_nvs
	if [ -z "$(UPLOAD_PORT)" ]; then \
		echo "Error: UPLOAD_PORT is required for erase-nvs" >&2; \
		exit 1; \
	elif [ -z "$(NVS_OFFSET)" ] || [ -z "$(NVS_SIZE)" ]; then \
		echo "Error: could not resolve NVS partition from $(PARTITIONS_FILE)" >&2; \
		exit 1; \
	elif [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Erasing NVS partition (offset=$(NVS_OFFSET), size=$(NVS_SIZE)) via local esptool"; \
		./.scripts/pio_local.sh pkg exec -p tool-esptoolpy -- esptool.py --port "$(UPLOAD_PORT)" erase_region "$(NVS_OFFSET)" "$(NVS_SIZE)"; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Erasing NVS partition (offset=$(NVS_OFFSET), size=$(NVS_SIZE)) via PlatformIO esptool"; \
		pio pkg exec -p tool-esptoolpy -- esptool.py --port "$(UPLOAD_PORT)" erase_region "$(NVS_OFFSET)" "$(NVS_SIZE)"; \
	else \
		echo "Error: no supported firmware tool found for erase-nvs (expected ./.scripts/pio_local.sh or pio)." >&2; \
		exit 1; \
	fi
endef

.PHONY: build flash monitor erase-nvs

build:
	@set -e; $(run_firmware_build)

flash:
	@set -e; $(run_firmware_flash)

monitor:
	@set -e; $(run_firmware_monitor)

erase-nvs:
	@set -e; $(run_firmware_erase_nvs)
