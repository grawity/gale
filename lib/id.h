#ifndef ID_H
#define ID_H

#include "common.h"
#include "sign.h"
#include "file.h"

struct auth_id {
	struct auth_id **ptr,*left,*right;	/* tree linkage */
	int version;				/* key version */
	struct gale_text name,comment;

	/* private key stuff */
	R_RSA_PRIVATE_KEY *private;
	struct inode priv_source;

	/* public key stuff */
	R_RSA_PUBLIC_KEY *public;
	int trusted;				/* inherently trusted? */
	struct gale_time sign_time;		/* when it was signed */
	struct gale_time expire_time;		/* when it expires */
	struct gale_time find_time;		/* when we last looked */
	struct signature sig;			/* signature for public key */
	struct inode source;			/* where it came from */
};

void _ga_warn_id(struct gale_text,...);

#endif
