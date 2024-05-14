CC      = gcc
CFLAGS  = -g ../SmartEVSE-3/src/monocypher-ed25519.c ../SmartEVSE-3/src/monocypher.c -I ../SmartEVSE-3/include/
RM      = rm -f

default: all

all: sign verify genkeys

sign: sign.c
	$(CC) $(CFLAGS) -o sign sign.c

verify: verify.c
	$(CC) $(CFLAGS) -o verify verify.c

genkeys: genkeys.c
	$(CC) $(CFLAGS) -o genkeys genkeys.c

clean veryclean:
	$(RM) sign verify genkeys

