#include "gale/key.h"
#include "key_i.h"

int main(int argc,char *argv[]) {
	struct gale_data key;
	struct gale_time now = gale_time_now();
	struct gale_key_assertion *ass;

	gale_init("crypto_test",argc,argv);

	key = gale_read_file(G_("/home/egnor/etc/gale/auth/trusted/ROOT"),65536,1,NULL);
	ass = gale_key_assert(key,1);
	if (gale_key_public(gale_key_handle(G_("ROOT")),now) != ass) 
		return 1;

	key = gale_read_file(G_("/home/egnor/etc/gale/auth/local/slashsite.gateway@gale.org"),65536,1,NULL);
	ass = gale_key_assert(key,0);
	if (gale_key_public(gale_key_handle(G_("gateway.slashsite@gale.org")),now) != ass) return 1;

	key = gale_read_file(G_("/home/egnor/.gale/auth/private/slashsite.gateway@gale.org"),65536,1,NULL);
	ass = gale_key_assert(key,1);
	if (gale_key_private(gale_key_handle(G_("gateway.slashsite@gale.org"))) != ass) return 1;

	key = gale_read_file(G_("/home/egnor/.gale/auth/private/ROOT"),65536,1,NULL);
	ass = gale_key_assert(key,1);
	if (gale_key_private(gale_key_handle(G_("ROOT"))) != ass) 
		return 1;

	return 0;
}
