import os, shutil

os.system("pio run -e ch32")
shutil.copy('.pio/build/ch32/firmware.bin', 'data/CH32V203.bin')
os.system("pio run -e v4 -t upload")
