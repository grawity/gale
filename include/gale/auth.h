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

void auth_id_gen(struct auth_id *,struct gale_text comment);

void export_auth_id(struct auth_id *,struct gale_data *data,int private);
void import_auth_id(struct auth_id **,struct gale_data data,int private);

void auth_sign(struct auth_id *,
               struct gale_data data,struct gale_data *sig);
void auth_verify(struct auth_id **,
                 struct gale_data data,struct gale_data sig);

/* Don't use this. */
void _auth_sign(struct auth_id *,
                struct gale_data data,struct gale_data *sig);

void auth_encrypt(int num,struct auth_id **,
                  struct gale_data plain,struct gale_data *cipher);
void auth_decrypt(struct auth_id **,
                  struct gale_data cipher,struct gale_data *plain);

#endif
