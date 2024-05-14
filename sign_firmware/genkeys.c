#include <stdio.h>
#include <monocypher-ed25519.h>
#include <string.h>
#include <stdlib.h>
#include <sys/random.h>
#include <sys/stat.h>

#define dump(X)   for (int i= 0; i< sizeof(X); i++) sprintf(Str + 2*i, "%02x",X[i]); printf("%s.\n",Str);

int main() {
    // Generate a key pair
    uint8_t public_key[32];
    uint8_t secret_key[64];
    uint8_t seed[32];
    unsigned char Str[128];
    if (getrandom(seed, sizeof(seed), 0) != sizeof(seed)) {
            printf("ERROR: could not get a good seed!.\n");
            exit(1);
    }
    printf("Seed:       ");
    dump(seed);
    crypto_ed25519_key_pair(secret_key, public_key, seed);
    
    printf("Public key: ");
    dump(public_key);
    //write to file
    FILE *write_ptr;
    write_ptr = fopen("public.key.txt","w");
    fwrite(Str,sizeof(Str),1,write_ptr);
    fwrite("\n",1,1,write_ptr);
    fclose(write_ptr);

    printf("Secret key: ");
    dump(secret_key);
    //rw permissions for owner only
    umask(0066);

    write_ptr = fopen("secret.key.txt","w");
    fwrite(Str,sizeof(Str),1,write_ptr);
    fwrite("\n",1,1,write_ptr);
    fclose(write_ptr);
}
