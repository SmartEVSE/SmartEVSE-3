#this script will be run by platformio.ini from its native directory
import os, sys

#check for the two files we need to be able to keep updating the firmware by the /update endpoint:
if not os.path.isfile("data/update2.html"):
    sys.exit(1)
if not os.path.isfile("data/app.js"):
    sys.exit(2)
os.system('rm -rf pack.tmp')
os.system('mkdir pack.tmp')
#    sys.exit(3)
if os.system('cc -o pack.tmp/pack src/pack.c'):
    sys.exit(4)
if os.system('cp -a data pack.tmp'):
    sys.exit(5)
if os.system('gzip pack.tmp/data/*'):
    sys.exit(6)
if os.system('gunzip pack.tmp/data/zones.csv'):
    sys.exit(7)
if os.system('cd pack.tmp; ./pack data/* >../src/packed_fs.c'):
    sys.exit(8)
if os.system('rm -rf pack.tmp'):
    sys.exit(9)
