BLUEPAD32_GITHUB:=https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template
BLUEPAD32_VERSION:=4.2.0
BLUEPAD32_BUILD_FOLDER:=build/esp-idf-arduino-bluepad32-template

MAESTRO_GITHUB:=https://github.com/pololu/maestro-arduino
MAESTRO_VERSION:=1.0.0
MAESTRO_BUILD_FOLDER:=build/maestro-arduino

SABERTOOTH_BUILD_FOLDER:=build/Sabertooth
SABERTOOTH_VERSION:=
SABERTOOTH_GITHUB:=https://www.dimensionengineering.com/software/SabertoothArduinoLibraries.zip

MP3_GITHUB:=https://github.com/sansumbrella/MP3Trigger-for-Arduino
MP3_VERSION:=master
MP3_BUILD_FOLDER:=build/MP3Trigger

GHOSTL_GITHUB:=https://github.com/dok-net/ghostl
GHOSTL_VERSION:=1.0.1
GHOSTL_BUILD_FOLDER:=build/ghostl

SOFTWARESERIAL_GITHUB:=https://github.com/plerup/espsoftwareserial
SOFTWARESERIAL_VERSION:=8.2.0
SOFTWARESERIAL_BUILD_FOLDER:=build/espsoftwareserial

UNITS_VERSION:=v2.3.3

FMT_VERSION:=11.0.1

WPILIB_GITHUB:=https://github.com/wpilibsuite/allwpilib
WPILIB_VERSION:=v2024.3.2
WPILIB_BUILD_FOLDER:=build/allwpilib

IDF_TOOLS_PATH:=D:\Espressif\

setup::
	mkdir -p build components main patches

gitsync:: setup
ifndef GITHUB_URL
	$(error GITHUB_URL is not set)
endif
ifndef GITHUB_BRANCH
	$(error GITHUB_BRANCH is not set)
endif
ifndef GITHUB_FOLDER
	$(error GITHUB_FOLDER is not set)
endif
	if [ ! -d "$(GITHUB_FOLDER)/_new" ]; then \
		git clone --depth 1 --branch $(GITHUB_BRANCH) $(GITHUB_URL) "$(GITHUB_FOLDER)/_new" ; \
	else \
		git -C "$(GITHUB_FOLDER)/_new" stash ; \
		git -C "$(GITHUB_FOLDER)/_new" pull origin $(GITHUB_BRANCH) ; \
	fi ;\
	rm -rf "$(GITHUB_FOLDER)/_org" ; \
	cp -r "$(GITHUB_FOLDER)/_new" "$(GITHUB_FOLDER)/_org"

make-patch::
ifndef GITHUB_FOLDER
	$(error GITHUB_FOLDER is not set)
endif
	diff -Nruw --strip-trailing-cr --exclude=.git "$(GITHUB_FOLDER)/_org/" "$(GITHUB_FOLDER)/_new/" > "patches/components/$(shell basename $(GITHUB_FOLDER)).patch" || true

apply-patch::
ifndef GITHUB_FOLDER
	$(error GITHUB_FOLDER is not set)
endif
	patch -l -f -d "$(GITHUB_FOLDER)" -p2 < patches/components/$(shell basename $(GITHUB_FOLDER)).patch
	rm -rf "$(GITHUB_FOLDER)/_org/.git" "components/$(shell basename $(GITHUB_FOLDER))"
	mv "$(GITHUB_FOLDER)/_org" "components/$(shell basename $(GITHUB_FOLDER))"
	rm -rf "$(GITHUB_FOLDER)"

# Platform.IO is used for builds in VSCode, if it's present we need to clean it up when updating to a new version of Bluepad32
platformio-clean::
	rm -rf $(HOME)/.platformio/ $(HOME)/.espressif .pio/

# -- Bluepad32
bluepad32-clean::
	rm -rf "$(BLUEPAD32_BUILD_FOLDER)"

bluepad32-download::
	GITHUB_URL=$(BLUEPAD32_GITHUB) GITHUB_BRANCH=$(BLUEPAD32_VERSION) GITHUB_FOLDER=$(BLUEPAD32_BUILD_FOLDER) "$(MAKE)" gitsync
	git -C "$(BLUEPAD32_BUILD_FOLDER)/_org" submodule update --init --recursive

bluepad32:: bluepad32-clean bluepad32-download
	for dir in $(BLUEPAD32_BUILD_FOLDER)/_org/components/*; do \
		if [ -d $$dir ]; then \
			mkdir -p components/$$(basename $$dir); \
			cp -R $$dir/* components/$$(basename $$dir); \
			find components/$$(basename $$dir) -name ".git" -type d -exec rm -rf {} +; \
		fi; \
	done
	cp -R $(BLUEPAD32_BUILD_FOLDER)/_org/patches/ patches/
	for f in platformio.ini sdkconfig.defaults; do \
		if [ ! -f $$f ]; then \
			cp -R $(BLUEPAD32_BUILD_FOLDER)/_org/$$f $$f; \
		fi; \
	done ; \
	rm -rf $(BLUEPAD32_BUILD_FOLDER)

# -- Sabertooth
sabertooth-clean::
	rm -rf $(SABERTOOTH_BUILD_FOLDER)

sabertooth-download:: setup
	if [ ! -d "$(SABERTOOTH_BUILD_FOLDER)" ]; then \
		mkdir -p $(SABERTOOTH_BUILD_FOLDER) ; \
		curl -o build/SabertoothArduinoLibraries.zip $(SABERTOOTH_GITHUB) ; \
		unzip -o build/SabertoothArduinoLibraries.zip -d $(SABERTOOTH_BUILD_FOLDER)/_new ; \
		rm build/SabertoothArduinoLibraries.zip ; \
		rm -rf $(SABERTOOTH_BUILD_FOLDER)/_org ; \
		cp -r $(SABERTOOTH_BUILD_FOLDER)/_new $(SABERTOOTH_BUILD_FOLDER)/_org ; \
	fi
	for f in Sabertooth/Sabertooth.h; do \
		for d in _new _org; do \
			dos2unix $(SABERTOOTH_BUILD_FOLDER)/$$d/$$f ; \
		done; \
	done

sabertooth:: sabertooth-clean sabertooth-download
	GITHUB_FOLDER=$(SABERTOOTH_BUILD_FOLDER) "$(MAKE)" apply-patch
	mv components/Sabertooth $(SABERTOOTH_BUILD_FOLDER)
	mkdir -p components/Sabertooth
	mv $(SABERTOOTH_BUILD_FOLDER)/Sabertooth/ components/
	rm -rf $(SABERTOOTH_BUILD_FOLDER)

sabertooth-patch::
	GITHUB_FOLDER=$(SABERTOOTH_BUILD_FOLDER) "$(MAKE)" make-patch

# -- Maestro
maestro-clean::
	rm -rf $(MAESTRO_BUILD_FOLDER)

maestro-download::
	GITHUB_URL=$(MAESTRO_GITHUB) GITHUB_BRANCH=$(MAESTRO_VERSION) GITHUB_FOLDER=$(MAESTRO_BUILD_FOLDER) "$(MAKE)" gitsync

maestro:: maestro-clean maestro-download 
	GITHUB_FOLDER=$(MAESTRO_BUILD_FOLDER) "$(MAKE)" apply-patch

maestro-patch::
	GITHUB_FOLDER=$(MAESTRO_BUILD_FOLDER) "$(MAKE)" make-patch

# -- MP3Trigger
mp3-clean::
	rm -rf $(MP3_BUILD_FOLDER)

mp3-download::
	GITHUB_URL=$(MP3_GITHUB) GITHUB_BRANCH=$(MP3_VERSION) GITHUB_FOLDER=$(MP3_BUILD_FOLDER) "$(MAKE)" gitsync

mp3:: mp3-clean mp3-download 
	GITHUB_FOLDER=$(MP3_BUILD_FOLDER) "$(MAKE)" apply-patch

mp3-patch::
	GITHUB_FOLDER=$(MP3_BUILD_FOLDER) "$(MAKE)" make-patch

# -- Ghostl
# https://github.com/plerup/espsoftwareserial/issues/305
ghostl-clean::
	rm -rf $(GHOSTL_BUILD_FOLDER)

ghostl-download::
	GITHUB_URL=$(GHOSTL_GITHUB) GITHUB_BRANCH=$(GHOSTL_VERSION) GITHUB_FOLDER=$(GHOSTL_BUILD_FOLDER) "$(MAKE)" gitsync

ghostl:: ghostl-clean ghostl-download
	GITHUB_FOLDER=$(GHOSTL_BUILD_FOLDER) "$(MAKE)" apply-patch

ghostl-patch::
	GITHUB_FOLDER=$(GHOSTL_BUILD_FOLDER) "$(MAKE)" make-patch

# -- SoftwareSerial
softwareserial-clean::
	rm -rf $(SOFTWARESERIAL_BUILD_FOLDER)

softwareserial-download::
	GITHUB_URL=$(SOFTWARESERIAL_GITHUB) GITHUB_BRANCH=$(SOFTWARESERIAL_VERSION) GITHUB_FOLDER=$(SOFTWARESERIAL_BUILD_FOLDER) "$(MAKE)" gitsync

softwareserial:: ghostl softwareserial-clean softwareserial-download
	GITHUB_FOLDER=$(SOFTWARESERIAL_BUILD_FOLDER) "$(MAKE)" apply-patch

softwareserial-patch::
	GITHUB_FOLDER=$(SOFTWARESERIAL_BUILD_FOLDER) "$(MAKE)" make-patch

fmt:: setup
	if [ ! -d "build/fmt" ]; then \
		git clone --depth 1 --branch $(FMT_VERSION) https://github.com/fmtlib/fmt build/fmt ;\
	else \
		git -C build/fmt pull origin master; \
	fi
	rm -rf build/fmt/.git components/fmt
	mv build/fmt components/fmt
	echo 'idf_component_register(INCLUDE_DIRS "./include")\n' >> components/fmt/CMakeLists.txt


units:: setup
	if [ ! -d "build/units" ]; then \
		git clone --depth 1 --branch $(UNITS_VERSION) https://github.com/nholthaus/units build/units ;\
	else \
		git -C build/units pull origin master; \
	fi
	rm -rf build/units/.git components/units
	mv build/units components/units
# TODO: 	if(NOT CMAKE_BUILD_EARLY_EXPANSION) ... endif()
	echo 'idf_component_register(INCLUDE_DIRS "./include")\n' >> components/units/CMakeLists.txt
 
wpilibc:: setup
	if [ ! -d "build/allwpilib" ]; then \
		git clone --depth 1 --branch $(WPILIB_VERSION) https://github.com/wpilibsuite/allwpilib build/allwpilib ;\
	else \
		git -C build/allwpilib pull origin main; \
	fi
	rm -rf build/allwpilib/.git components/allwpilib
#	mv build/allwpilib/hal/src/main/native/include/hal main/include/hal
#	mv build/allwpilib/wpiutil/src/main/native/include/wpi/ main/include/wpi
#	mv build/allwpilib/wpimath/src/main/native/include/units/ main/include/units
#	mv build/allwpilib/wpimath/src/main/native/include/frc/MathUtil.h main/include/MathUtil.h

setup-all:: sabertooth maestro mp3 softwareserial bluepad32

build::
	IDF_PATH=D:\Espressif $$IDF_PATH\idf_cmd_init.bat;\
	idf.py build

flash::
	idf.py flash monitor