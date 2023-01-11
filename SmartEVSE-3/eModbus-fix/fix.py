#this script will be run by platformio.ini from its native directory
import os
os.system('cp eModbus-fix/RTUutils.cpp .pio/libdeps/release/eModbus/src/RTUutils.cpp')

