/* gauth.h -- new style authentication -- in flux, not yet documented */

#ifndef GALE_GAUTH_H
#define GALE_GAUTH_H

#include "gale/util.h"

struct auth_id;

void init_auth_id(struct auth_id **,const char *name);
void free_auth_id(struct auth_id *);
const char *auth_id_name(struct auth_id *);
const char *auth_id_comment(struct auth_id *);

int auth_id_public(struct auth_id *);
int auth_id_private(struct auth_id *);

void auth_id_gen(struct auth_id *,const char *comment);

void export_auth_id(struct auth_id *,struct gale_data *data,int private);
void import_auth_id(struct auth_id **,struct gale_data data,int private);

void auth_sign(struct auth_id *,
               struct gale_data data,struct gale_data *sig);
void auth_verify(struct auth_id **,
                 struct gale_data data,struct gale_data sig);

void auth_encrypt(int num,struct auth_id **,
                  struct gale_data plain,struct gale_data *cipher);
void auth_decrypt(struct auth_id **,
                  struct gale_data cipher,struct gale_data *plain);

#define armor_len(blen) (((blen) * 8 + 7) / 6)
void armor(const byte *bin,int blen,char *txt);

#define dearmor_len(tlen) ((tlen) * 6 / 8)
void dearmor(const char *txt,int tlen,byte *bin);

#endif
