/* auth.h -- low-level authentication and encryption. */

#ifndef GALE_AUTH_H
#define GALE_AUTH_H

#include "gale/core.h"
#include "gale/types.h"

/* Not yet documented, sorry. */

struct auth_id;

typedef int auth_hook(struct auth_id *);

void init_auth_id(struct auth_id **,struct gale_text name);
struct gale_text auth_id_name(struct auth_id *);
struct gale_text auth_id_comment(struct auth_id *);

int auth_id_public(struct auth_id *);
int auth_id_private(struct auth_id *);

void auth_id_gen(struct auth_id *,struct gale_group extra);

void export_auth_id(struct auth_id *,struct gale_data *data,int private);
void import_auth_id(struct auth_id **,struct gale_data data,int private);

#define AUTH_SIGN_NORMAL 0
#define AUTH_SIGN_SELF 1
int auth_sign(struct gale_group *,struct auth_id *,int flag);
struct auth_id *auth_verify(struct gale_group *);

int auth_encrypt(struct gale_group *grp,int num,struct auth_id **ids);
struct auth_id *auth_decrypt(struct gale_group *grp);

#endif
