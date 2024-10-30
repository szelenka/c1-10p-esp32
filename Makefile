MAESTRO_VERSION:=1.0.0

sabertooth::
	curl -o build/SabertoothArduinoLibraries.zip https://www.dimensionengineering.com/software/SabertoothArduinoLibraries.zip
	unzip -o build/SabertoothArduinoLibraries.zip -d build/

maestro::
	if [ ! -d "build/maestro-arduino" ]; then \
		git clone --depth 1 --branch $(MAESTRO_VERSION) https://github.com/pololu/maestro-arduino build/maestro-arduino; \
	else \
		git -C build/maestro-arduino pull origin $(MAESTRO_VERSION); \
	fi

mp3::
	if [ ! -d "build/MP3Trigger-for-Arduino" ]; then \
		git clone --depth 1 https://github.com/sansumbrella/MP3Trigger-for-Arduino.git build/MP3Trigger-for-Arduino; \
	else \
		git -C build/MP3Trigger-for-Arduino pull origin master; \
	fi