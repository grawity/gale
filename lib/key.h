#ifndef KEY_H
#define KEY_H

#include "common.h"
#include "file.h"

#define EXPORT_NORMAL 0		/* if not certified export stub; else TRUSTED */
#define EXPORT_TRUSTED 1	/* export key with any certification */
#define EXPORT_SIGN 2		/* always export key without certification */
#define EXPORT_STUB 3		/* always export a stub key */

#define IMPORT_NORMAL 0		/* arbitrary untrusted data */
#define IMPORT_TRUSTED 1	/* from a trusted, valid source */

void _ga_import_pub(struct auth_id **,struct gale_data key,
                    struct inode *source,int flag);
void _ga_export_pub(struct auth_id *,struct gale_data *key,int flag);
int _ga_find_pub(struct auth_id *);
void _ga_sign_pub(struct auth_id *,struct gale_time expire);
int _ga_trust_pub(struct auth_id *);

void _ga_import_priv(struct auth_id **,struct gale_data key);
void _ga_export_priv(struct auth_id *,struct gale_data *key);

#endif
