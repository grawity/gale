#ifndef CRYPTO_I_H
#define CRYPTO_I_H

#include "gale/crypto.h"
#include "gale/types.h"

#include <openssl/crypto.h>
#include <openssl/rsa.h>

void crypto_i_seed(void);
void crypto_i_error(void);

struct gale_text crypto_i_rsa(struct gale_group,RSA *);
int crypto_i_public_valid(RSA *);
int crypto_i_private_valid(RSA *);

/* Although these restrictions do not necessarily apply to this implementation, 
 * they are used to compute field sizes and such in the key storage, and so     
 * must be retained. */
#define GALE_RSA_MODULUS_BITS 1024
#define GALE_RSA_MODULUS_LEN ((GALE_RSA_MODULUS_BITS + 7) / 8)
#define GALE_RSA_PRIME_BITS ((GALE_RSA_MODULUS_BITS + 1) / 2)
#define GALE_RSA_PRIME_LEN ((GALE_RSA_PRIME_BITS + 7) / 8)
#define GALE_ENCRYPTED_KEY_LEN GALE_RSA_MODULUS_LEN
#define GALE_SIGNATURE_LEN GALE_RSA_MODULUS_LEN

/* Magic number for embedded signatures */
static const byte sig_magic[] = { 0x68, 0x13, 0x01, 0x00 };

#endif
