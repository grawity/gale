#ifndef KEY_H
#define KEY_H

#include "common.h"
#include "file.h"

#define SAFE_KEY 1

#define EXPORT_NORMAL 0		/* if not certified export stub; else TRUSTED */
#define EXPORT_TRUSTED 1	/* export key with any certification */
#define EXPORT_STUB 3		/* always export a stub key */

#define IMPORT_NORMAL 0		/* arbitrary untrusted data */
#define IMPORT_TRUSTED 1	/* from a trusted, valid source */

struct gale_text _ga_signer(struct gale_text);

void _ga_import_pub(struct auth_id **,struct gale_data key,
                    struct inode *source,int flag);
void _ga_export_pub(struct auth_id *,struct gale_data *key,int flag);
int _ga_find_pub(struct auth_id *);
void _ga_sign_pub(struct auth_id *,struct gale_time expire);
int _ga_trust_pub(struct auth_id *);

int _ga_pub_equal(struct gale_group,struct gale_group);
int _ga_pub_older(struct gale_group,struct gale_group);
int _ga_pub_rsa(struct gale_group,R_RSA_PUBLIC_KEY *);

void _ga_import_priv(struct auth_id **,struct gale_data key,struct inode *src);
void _ga_export_priv(struct auth_id *,struct gale_data *key);

int _ga_priv_rsa(struct gale_group,R_RSA_PRIVATE_KEY *);

void _ga_encrypt(int num,struct auth_id **ids,
                 struct gale_data plain,struct gale_data *cipher);
void _ga_decrypt(struct auth_id **id,
                 struct gale_data cipher,struct gale_data *plain);

#endif
