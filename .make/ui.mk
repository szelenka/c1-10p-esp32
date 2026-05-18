# ============================================================================
# Telemetry UI bridge and URDF import
# ============================================================================

UI_HOST ?= 127.0.0.1
UI_PORT ?= 8765
UI_SERIAL ?=
UI_BAUD ?= 115200

run-ui-bridge:
	@set -e; \
	if [ -f "$(CURDIR)/.venv/bin/python" ]; then \
		VENV_PY="$(CURDIR)/.venv/bin/python"; \
	elif [ -f "$(CURDIR)/.venv/Scripts/python.exe" ]; then \
		VENV_PY="$(CURDIR)/.venv/Scripts/python.exe"; \
	else \
		echo "No .venv found at project root — creating ..."; \
		"$(PYTHON)" -m venv .venv; \
		if [ -f "$(CURDIR)/.venv/bin/python" ]; then \
			VENV_PY="$(CURDIR)/.venv/bin/python"; \
		elif [ -f "$(CURDIR)/.venv/Scripts/python.exe" ]; then \
			VENV_PY="$(CURDIR)/.venv/Scripts/python.exe"; \
		else \
			echo "Error: created .venv, but could not find its Python executable."; \
			exit 1; \
		fi; \
	fi; \
	if ! "$$VENV_PY" -c "import aiohttp, serial" >/dev/null 2>&1; then \
		echo "Installing UI bridge dependencies..."; \
		"$$VENV_PY" -m pip install -r tools/telemetry_ui/requirements.txt; \
	fi; \
	SERIAL="$(UI_SERIAL)"; \
	if [ -z "$$SERIAL" ]; then \
		CANDIDATES=$$("$$VENV_PY" .scripts/detect_serial_ports.py); \
		COUNT=$$(echo "$$CANDIDATES" | grep -c . 2>/dev/null || true); \
		if [ "$$COUNT" -eq 1 ]; then \
			SERIAL="$$CANDIDATES"; \
			echo "Auto-detected serial port: $$SERIAL"; \
		elif [ "$$COUNT" -gt 1 ]; then \
			echo "Multiple serial port candidates found:"; \
			echo "$$CANDIDATES" | while read -r p; do echo "  $$p"; done; \
			echo ""; \
			echo "Set UI_SERIAL to select one, e.g.:"; \
			echo "  make run-ui-bridge UI_SERIAL=$$(echo "$$CANDIDATES" | head -1)"; \
			exit 1; \
		else \
			echo "No USB serial ports detected by pyserial."; \
		fi; \
	fi; \
	PID=$$(lsof -ti tcp:$(UI_PORT) 2>/dev/null || true); \
	if [ -n "$$PID" ]; then \
		echo "Killing existing process on port $(UI_PORT) (pid $$PID)..."; \
		kill $$PID 2>/dev/null || true; \
		sleep 1; \
	fi; \
	echo "Telemetry UI URL: http://$(UI_HOST):$(UI_PORT)"; \
	echo "Starting UI bridge (serial='$$SERIAL' baud=$(UI_BAUD))"; \
	cd tools/telemetry_ui; \
	PY="$$VENV_PY"; \
	if [ -n "$$SERIAL" ]; then \
		CMD="$$PY app.py --host \"$(UI_HOST)\" --port \"$(UI_PORT)\" --serial \"$$SERIAL\" --baud \"$(UI_BAUD)\""; \
	else \
		CMD="$$PY app.py --host \"$(UI_HOST)\" --port \"$(UI_PORT)\" --baud \"$(UI_BAUD)\""; \
	fi; \
	set +e; \
	eval "$$CMD"; \
	RC=$$?; \
	set -e; \
	if [ $$RC -eq 130 ]; then \
		echo "UI bridge stopped (Ctrl+C)"; \
		exit 0; \
	fi; \
	exit $$RC

# ── URDF import from Fusion 360 export ──────────────────────────────────
FUSION_EXPORT_DIR ?=

import-urdf:
	@test -n "$(FUSION_EXPORT_DIR)" || { echo "Usage: make import-urdf FUSION_EXPORT_DIR=/path/to/fusion2urdf_description"; exit 1; }
	@test -d "$(FUSION_EXPORT_DIR)/urdf" || { echo "Error: $(FUSION_EXPORT_DIR)/urdf not found"; exit 1; }
	@test -d "$(FUSION_EXPORT_DIR)/meshes" || { echo "Error: $(FUSION_EXPORT_DIR)/meshes not found"; exit 1; }
	@echo "Importing meshes from $(FUSION_EXPORT_DIR)/meshes/ ..."
	rm -rf description/fusion2urdf/meshes
	mkdir -p description/fusion2urdf/meshes
	cp "$(FUSION_EXPORT_DIR)"/meshes/*.stl description/fusion2urdf/meshes/
	@echo "Resolving xacro -> URDF ..."
	$(PYTHON) .scripts/xacro2urdf.py "$(FUSION_EXPORT_DIR)" description/fusion2urdf/chopper_fusion.urdf
	@echo ""
	@echo "Imported $$(ls description/fusion2urdf/meshes/*.stl | wc -l | tr -d ' ') STL meshes"
	@echo "URDF: description/fusion2urdf/chopper_fusion.urdf"
	@echo ""
	@echo "Check tools/telemetry_ui/joint_mapping.json if joint/link names changed."

# ── UI validation gates ─────────────────────────────────────────────────

check-ui:
	@mkdir -p build/pycache
	@echo "── Python syntax check ──"
	@PYTHONPYCACHEPREFIX="$(CURDIR)/build/pycache" $(PYTHON) -m py_compile tools/telemetry_ui/app.py
	@echo "PASS: app.py"
	@echo "── JS syntax check ──"
	@node --check tools/telemetry_ui/web/app.js
	@node --check tools/telemetry_ui/web/render3d.js 2>/dev/null || echo "SKIP: render3d.js (ES module, requires import map)"
	@echo "── Joint mapping validation ──"
	@$(MAKE) --no-print-directory check-ui-mapping
	@echo "── Telemetry firmware↔UI sync ──"
	@$(MAKE) --no-print-directory check-telemetry-sync
	@echo "── UI check complete ──"

check-ui-mapping:
	@$(PYTHON) ./.scripts/check_ui_mapping.py

check-telemetry-sync:
	@$(PYTHON) ./.scripts/check_telemetry_sync.py

test-ui-integration:
	@echo "── Telemetry parser integration test ──"
	@$(PYTHON) .scripts/test_ui_integration.py
