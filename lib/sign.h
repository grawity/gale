#ifndef SIGN_H
#define SIGN_H

#include "common.h"

struct signature {
	struct auth_id *id;
	struct gale_data sig;
};

void _ga_import_sig(struct signature *,struct gale_data sig);
void _ga_export_sig(struct signature *,struct gale_data *sig,int flag);
void _ga_init_sig(struct signature *);

void _ga_sign(struct signature *,struct auth_id *,struct gale_data data);
int _ga_verify(struct signature *,struct gale_data data);

#endif
