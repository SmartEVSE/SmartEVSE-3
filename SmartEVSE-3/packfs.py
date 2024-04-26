#this script will be run by platformio.ini from its native directory
import os, sys, gzip

#check for the two files we need to be able to keep updating the firmware by the /update endpoint:
if not os.path.isfile("data/update2.html"):
    sys.exit(1)
if not os.path.isfile("data/app.js"):
    sys.exit(2)
os.system('rm -rf pack.tmp')
os.system('mkdir pack.tmp')
#    sys.exit(3)
if os.system('cp -a data pack.tmp'):
    sys.exit(5)

# now gzip the stuff except zones.csv since this file is not served by mongoose but directly accessed:
for file in os.listdir("pack.tmp/data"):
    filename = os.fsdecode(file)
    if not filename == "zones.csv":
        with open('pack.tmp/data/' + filename, 'rb') as f_in, gzip.open('pack.tmp/data/' + filename + '.gz', 'wb') as f_out:
            f_out.writelines(f_in)
            os.remove('pack.tmp/data/' + filename)
        continue
    else:
        continue

if os.system('cd pack.tmp; python ../pack.py data/* >../src/packed_fs.c'):
    sys.exit(8)
if os.system('rm -rf pack.tmp'):
    sys.exit(9)
