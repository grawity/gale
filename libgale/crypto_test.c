#include "gale/crypto.h"

static void print_array(struct gale_text label,const struct gale_text *arr) {
	gale_print(stdout,0,label);
	gale_print(stdout,0,G_(" => "));
	if (arr->l) gale_print(stdout,0,*arr++);
	while (arr->l) {
		gale_print(stdout,0,G_(", "));
		gale_print(stdout,0,*arr++);
	}
	gale_print(stdout,0,G_("\n"));
}

static void print_group(struct gale_text label,struct gale_group group) {
	gale_print(stdout,0,label);
	gale_print(stdout,0,G_(" => "));
	gale_print(stdout,0,gale_print_group(group,4 + label.l));
	gale_print(stdout,0,G_("\n"));
}

int main(int argc,char *argv[]) {
	struct gale_group keys[3],pubkeys[2];
	const struct gale_text *target;
	struct gale_group plain,cipher;
	struct gale_fragment thing;
	struct gale_data message;
	const struct gale_data *signatures;

	gale_init("crypto_test",argc,argv);
	keys[0] = gale_crypto_generate(G_("k1"));
	keys[1] = gale_crypto_generate(G_("k2"));
	keys[2] = gale_crypto_generate(G_("k2"));
	pubkeys[0] = gale_crypto_public(keys[0]);
	pubkeys[1] = gale_crypto_public(keys[1]);

    /*
	print_group(G_("keys[0]"),keys[0]);
	gale_print(stdout,0,G_("\n"));
	print_group(G_("pubkeys[0]"),pubkeys[0]);
	gale_print(stdout,0,G_("\n"));
	print_group(G_("keys[1]"),keys[1]);
	gale_print(stdout,0,G_("\n"));
	print_group(G_("pubkeys[1]"),pubkeys[1]);
	gale_print(stdout,0,G_("\n"));
    */

	plain = gale_group_empty();
	thing.name = G_("fragment.name");
	thing.type = frag_text;
	thing.value.text = G_("some plain text");
	gale_group_add(&plain,thing);

	cipher = plain;
	print_group(G_("plain"),cipher);
	if (!gale_crypto_seal(2,pubkeys,&cipher)) return 1;
	print_group(G_("cipher"),cipher);

	target = gale_crypto_target(cipher);
	if (NULL == target) return 1;
	print_array(G_("target"),target);

	gale_print(stdout,0,G_("expect \"invalid private key\" warning\n"));
	if (gale_crypto_open(pubkeys[0],&cipher)) return 1;

	gale_print(stdout,0,G_("expect warnings from \"rsa routines\"\n"));
	if (gale_crypto_open(keys[2],&cipher)) return 1;

	plain = cipher;
	if (!gale_crypto_open(keys[0],&plain)) return 1;
	print_group(G_("plain"),plain);

	plain = cipher;
	if (!gale_crypto_open(keys[1],&plain)) return 1;
	print_group(G_("plain"),plain);

	message = gale_text_as_data(G_("Hello world!"));
	signatures = gale_crypto_sign_raw(2,keys,message);
	if (NULL == signatures) return 1;

	gale_print(stdout,0,G_("expect warnings from \"rsa routines\"\n"));
	if (gale_crypto_verify_raw(1,0 + pubkeys,1 + signatures,message)
	|| !gale_crypto_verify_raw(1,1 + pubkeys,1 + signatures,message)
	|| !gale_crypto_verify_raw(2,pubkeys,signatures,message)) return 1;

	cipher = plain;
	print_group(G_("plain"),cipher);
	if (!gale_crypto_sign(1,1 + keys,&cipher)) return 1;
	print_group(G_("cipher"),cipher);
	target = gale_crypto_sender(cipher);
	if (NULL == target) return 1;
	print_array(G_("sender"),target);

	gale_print(stdout,0,G_("expect warnings from \"rsa routines\"\n"));
	if (gale_crypto_verify(1,2 + keys,cipher)) return 1;
	if (gale_crypto_verify(1,0 + keys,cipher)) return 1;
	if (!gale_crypto_verify(1,1 + keys,cipher)) return 1;
	plain = gale_crypto_original(cipher);
	print_group(G_("verified"),plain);

	cipher = plain;
	print_group(G_("plain"),cipher);
	if (!gale_crypto_sign(2,keys,&cipher)) return 1;
	print_group(G_("signed"),cipher);
	target = gale_crypto_sender(cipher);
	if (NULL == target) return 1;
	print_array(G_("sender"),target);

	gale_print(stdout,0,G_("expect warnings from \"rsa routines\"\n"));
	if (gale_crypto_verify(1,2 + keys,cipher)) return 1;
	if (!gale_crypto_verify(1,1 + keys,cipher)) return 1;
	if (!gale_crypto_verify(1,keys,cipher)) return 1;
	if (!gale_crypto_verify(2,keys,cipher)) return 1;
	print_group(G_("verified"),gale_crypto_original(cipher));

	return 0;
}
