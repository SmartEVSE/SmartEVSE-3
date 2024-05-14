#include <stdio.h>
#include <monocypher-ed25519.h>
#include <string.h>
#include <stdlib.h>
#include <sys/random.h>

//#define dump(X)   for (int i= 0; i< sizeof(X); i++) printf("%02x",X[i]); printf(".\n");

int main(int argc, char *argv[]) {
    long lSize;
    unsigned char * buffer;
    FILE *pFile;
    unsigned char signature[64];
    unsigned char message_hash[64];
    unsigned char secret_key[64];

    int result = 1;
    for (int i=0; i<sizeof(secret_key) || result != 1; i++) {
        result = sscanf(argv[1] + i*2, "%02x", (unsigned int *) &secret_key[i]);
    }

    pFile = fopen("../SmartEVSE-3/.pio/build/release/firmware.bin","rb");
    // obtain file size:
    fseek (pFile , 0 , SEEK_END);
    lSize = ftell (pFile);
    rewind(pFile);
    // allocate memory to contain the whole file:
    buffer = (char*) malloc (sizeof(char)*lSize);
    if (buffer == NULL) {fputs ("Memory error",stderr); exit (2);}
    // copy the file into the buffer:
    result = fread (buffer,1,lSize,pFile);
    if (result != lSize) {fputs ("Reading error",stderr); exit (3);}
    crypto_sha512(message_hash, buffer, lSize);
    fclose (pFile);

    crypto_ed25519_ph_sign(signature, secret_key, message_hash);
    pFile = fopen("../SmartEVSE-3/.pio/build/release/firmware.signed.bin","wb");
    fwrite(signature,sizeof(signature),1,pFile);
    fwrite(buffer,lSize,1,pFile);
    fclose(pFile);

    // terminate
    free (buffer);
}
