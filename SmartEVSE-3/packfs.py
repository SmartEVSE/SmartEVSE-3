#this script will be run by platformio.ini from its native directory
import os
os.system('mkdir pack.tmp')
os.system('cc -o pack.tmp/pack src/pack.c')
os.system('cp -a data pack.tmp')
os.system('gzip pack.tmp/data/*')
os.system('gunzip pack.tmp/data/zones.csv')
os.system('cd pack.tmp; ./pack data/* >../src/packed_fs.c')
os.system('rm -rf pack.tmp')

