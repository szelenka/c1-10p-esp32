BLUEPAD32_VERSION:=4.1.0
MAESTRO_VERSION:=1.0.0
IDF_TOOLS_PATH:=D:\Espressif\

setup::
	mkdir -p build components main patches

bluepad32::
	if [ ! -d "build/esp-idf-arduino-bluepad32-template" ]; then \
		git clone --depth 1 --recursive --branch $(BLUEPAD32_VERSION) https://gitlab.com/ricardoquesada/esp-idf-arduino-bluepad32-template.git build/esp-idf-arduino-bluepad32-template; \
	else \
		git -C build/esp-idf-arduino-bluepad32-template pull origin $(BLUEPAD32_VERSION); \
	fi
	cp -R build/esp-idf-arduino-bluepad32-template/components/ components
	cp -R build/esp-idf-arduino-bluepad32-template/patches patches
	for f in CMakeList.txt LICENSE platformio.ini sdkconfig.defaults; do \
		cp -R build/esp-idf-arduino-bluepad32-template/$$f $$f; \
	done

sabertooth::
	if [ ! -d "build/Sabertooth-for-ESP32" ]; then \
		git clone --depth 1	https://github.com/dominicklee/Sabertooth-for-ESP32 build/Sabertooth-for-ESP32; \
	else \
		git -C build/Sabertooth-for-ESP32 pull origin main; \
	fi
	rm -rf build/Sabertooth-for-ESP32/.git components/Sabertooth
	mv build/Sabertooth-for-ESP32/Sabertooth components/Sabertooth
	rm -rf build/Sabertooth-for-ESP32
	echo 'set(srcs "Sabertooth.cpp")\n\nidf_component_register(SRCS "${srcs}" INCLUDE_DIRS ".")\n' > components/Sabertooth/CMakeLists.txt

maestro::
	if [ ! -d "build/maestro-arduino" ]; then \
		git clone --depth 1 --branch $(MAESTRO_VERSION) https://github.com/pololu/maestro-arduino build/maestro-arduino; \
	else \
		git -C build/maestro-arduino pull origin $(MAESTRO_VERSION); \
	fi
	rm -rf build/maestro-arduino/.git components/maestro-arduino
	mv build/maestro-arduino components/maestro-arduino
	echo 'set(srcs "PololuMaestro.cpp")\n\nidf_component_register(SRCS "${srcs}" INCLUDE_DIRS ".")\n' > components/maestro-arduino/CMakeLists.txt

mp3::
	if [ ! -d "build/MP3Trigger-for-Arduino" ]; then \
		git clone --depth 1 https://github.com/sansumbrella/MP3Trigger-for-Arduino.git build/MP3Trigger-for-Arduino; \
	else \
		git -C build/MP3Trigger-for-Arduino pull origin master; \
	fi
	rm -rf build/MP3Trigger-for-Arduino/.git components/MP3Trigger
	mv build/MP3Trigger-for-Arduino components/MP3Trigger
	echo 'set(srcs "MP3Trigger.cpp")\n\nidf_component_register(SRCS "${srcs}" INCLUDE_DIRS ".")\n' > components/MP3Trigger/CMakeLists.txt

# https://github.com/plerup/espsoftwareserial/issues/305
ghostl::
	if [ ! -d "build/ghostl" ]; then \
		git clone --depth 1 https://github.com/plerup/espsoftwareserial build/espsoftwareserial ;\
	else \
		git -C build/ghostl pull origin main; \
	fi
	rm -rf build/ghostl/.git components/ghostl
	mv build/ghostl components/ghostl
	echo 'set(srcs "src/FastScheduler.cpp")\n\nidf_component_register(SRCS "${srcs}" INCLUDE_DIRS "./src")\n' > components/espsoftwareserial/CMakeLists.txt


softwareserial:: ghostl
	if [ ! -d "build/espsoftwareserial" ]; then \
		git clone --depth 1 https://github.com/plerup/espsoftwareserial build/espsoftwareserial ;\
	else \
		git -C build/espsoftwareserial pull origin main; \
	fi
	rm -rf build/espsoftwareserial/.git components/espsoftwareserial
	mv build/espsoftwareserial components/espsoftwareserial
	echo 'set(srcs "src/SoftwareSerial.cpp")\n\nidf_component_register(SRCS "${srcs}" INCLUDE_DIRS "./src")\n' > components/espsoftwareserial/CMakeLists.txt

setup-all:: sabertooth maestro mp3 softwareserial bluepad32

build::
	IDF_PATH=D:\Espressif $$IDF_PATH\idf_cmd_init.bat;\
	idf.py build

flash::
	idf.py flash monitor