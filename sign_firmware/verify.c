#include <stdio.h>
#include <monocypher-ed25519.h>
#include <string.h>
#include <stdlib.h>
#include <sys/random.h>

#define dump(X)   for (int i= 0; i< sizeof(X); i++) printf("%02x",X[i]); printf(".\n");

int main() {
    long lSize;
    unsigned char * buffer;
    size_t result;
    FILE *pFile;
    unsigned char signature[64];
    unsigned char message_hash[64];
    unsigned char public_key[32];

    pFile = fopen("public.key.txt","r");
    buffer = malloc (2*sizeof(public_key)+1);
    result = fread (buffer,2*sizeof(public_key),1, pFile);
    buffer[2*sizeof(public_key)]='\0'; //terminate with null charachter
    for (int i=0; i<sizeof(public_key) || result != 1; i++) {
        result = sscanf(buffer + i*2, "%02x", (unsigned int *) &public_key[i]);
    }
    fclose(pFile);
    free(buffer);

    printf("Public key: ");
    dump(public_key);

    pFile = fopen("../SmartEVSE-3/.pio/build/release/firmware.signed.bin","rb");
    // obtain file size:
    fseek (pFile , 0 , SEEK_END);
    lSize = ftell (pFile);
    rewind(pFile);
    printf("FILE SIZE=%lu.\n", lSize);
    // allocate memory to contain the whole file:
    buffer = (char*) malloc (sizeof(char)*lSize);
    if (buffer == NULL) {fputs ("Memory error",stderr); exit (2);}
    result = fread (signature, sizeof(signature), 1, pFile);
    printf("Signature: ");
    dump(signature);
    if (result != 1) {fputs ("Reading signature error",stderr); exit (3);}
    // copy the rest of the file into the buffer:
    result = fread (buffer,lSize-sizeof(signature),1,pFile);
    if (result != 1) {fputs ("Reading error",stderr); exit (3);}
    crypto_sha512(message_hash, buffer, lSize-sizeof(signature));
    printf("Message hash: ");
    dump(message_hash);
    fclose (pFile);

    // Verify the signature
    int verification_result = crypto_ed25519_ph_check(signature, public_key, message_hash);

    if (verification_result == 0) {
        printf("Signature is valid!\n");
    } else {
        printf("Signature is invalid!\n");
    }
    // terminate
    free (buffer);
}
