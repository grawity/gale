#include <sys/types.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <pwd.h>

#include "global.h"
#include "rsaref.h"

#include "gale/all.h"

static R_RSA_PUBLIC_KEY pub_key;
static R_RSA_PRIVATE_KEY priv_key;

static void rsa_err(char *s) {
	(void) s;
	gale_alert(GALE_ERROR,/*"RSAREF failure."*/ s,0); /* XXX */
	exit(1);
}

static void get_random(R_RANDOM_STRUCT *rand) {
	R_DIGEST_CTX ctx;
	int needed,fd,len;
	char buf[MAX_DIGEST_LEN];

	fd = open("/dev/urandom",O_RDONLY);
	if (fd < 0) {
		struct stat buf;
		fd = open(dir_file(dot_gale,"random"),O_RDONLY);
		if (fd >= 0 && (fstat(fd,&buf) || (buf.st_mode & 077))) {
			gale_alert(GALE_WARNING,
				"world-readable \".gale/random\" file",0);
			if (fd >= 0) {
				close(fd);
				fd = -1;
			}
		}
		if (fd < 0) {
			gale_alert(GALE_WARNING,
			"You do not have a good random number source.",0);
		}
	}

	if (R_DigestInit(&ctx,DA_MD5))
		rsa_err("R_DigestInit");

	if (R_RandomInit(rand)) 
		rsa_err("R_RandomInit");
	if (R_GetRandomBytesNeeded(&needed,rand)) 
		rsa_err("R_GetRandomBytesNeeded");
	while (needed > 0) {
		len = fd < 0 ? -1 : read(fd,buf,sizeof(buf));
		if (len <= 0) {
			if (fd >= 0) {
				close(fd);
				fd = -1;
			}
			if (R_DigestFinal(&ctx,buf,&len))
				rsa_err("R_DigestFinal");
		}
		if (R_DigestUpdate(&ctx,buf,len))
			rsa_err("R_DigestUpdate");
		if (R_RandomUpdate(rand,buf,len))
			rsa_err("R_RandomUpdate");
		if (R_GetRandomBytesNeeded(&needed,rand))
			rsa_err("R_GetRandomBytesNeeded");
	}

	if (fd >= 0) close(fd);

	if (R_DigestFinal(&ctx,buf,&len)) rsa_err("R_DigestFinal");

	fd = open(dir_file(dot_gale,"random"),O_CREAT|O_TRUNC|O_WRONLY,0600);

	if (fd >= 0) {
		write(fd,buf,len);
		close(fd);
	}
}

static int read_file(const char *dir,int mode,const char *file) {
	int fd;
	sub_dir(dot_gale,dir,mode);
	fd = open(dir_file(dot_gale,file),O_RDONLY);
	up_dir(dot_gale);
	if (fd < 0 && errno == ENOENT) {
		sub_dir(sys_dir,dir,0);
		fd = open(dir_file(sys_dir,file),O_RDONLY);
		up_dir(sys_dir);
	}
	if (fd < 0 && errno != ENOENT)
		gale_alert(GALE_WARNING,file,errno);
	return fd;
}

static int read_pub_key(struct gale_id *id,R_RSA_PUBLIC_KEY *key) {
	const char *name = auth_id_name(id);
	int r,fd = read_file("public-keys",0777,name);
	const char *at = strchr(name,'@');

	if (fd < 0 && at && !strcmp(at + 1,getenv("GALE_DOMAIN"))) {
		char *user = gale_strndup(name,at - name);
		struct passwd *pwd = getpwnam(user);
		if (pwd) {
			char *tmp = gale_malloc(strlen(pwd->pw_dir) + 20);
			sprintf(tmp,"%s/.gale-public-key",pwd->pw_dir);
			fd = open(tmp,O_RDONLY);
			gale_free(tmp);
		}
		gale_free(user);
	}

	if (fd < 0) return -1;

	r = read(fd,key,sizeof(*key));
	close(fd);
	if (r < (int) sizeof(*key)) return -1;
	key->bits = ntohl(key->bits);
	return 0;
}

static int read_priv_key(struct gale_id *id,R_RSA_PRIVATE_KEY *key) {
	int r,fd = read_file("private-keys",0700,auth_id_name(id));
	if (fd < 0) return -1;
	r = read(fd,key,sizeof(*key));
	close(fd);
	if (r < (int) sizeof(*key)) return -1;
	key->bits = ntohl(key->bits);
	return 0;
}

static int write_file(const char *dir,int dmode,const char *file,int fmode) {
	int fd;
	sub_dir(dot_gale,dir,dmode);
	fd = creat(dir_file(dot_gale,file),fmode);
	up_dir(dot_gale);
	if (fd < 0) gale_alert(GALE_ERROR,file,errno);
	return fd;
}

static void check_dot_key(void) {
	struct stat dot,not_dot;
	int dot_st,not_dot_st;
	const char *dot_fn,*not_dot_fn,*domain,*username;
	char *tmp;

	domain = getenv("GALE_DOMAIN");
	username = getenv("LOGNAME");
	tmp = gale_malloc(strlen(domain) + strlen(username) + 2);
	sprintf(tmp,"%s@%s",username,domain);
	if (strcmp(auth_id_name(user_id),tmp)) {
		gale_free(tmp);
		return;
	}
	gale_free(tmp);

	dot_fn = dir_file(home_dir,".gale-public-key");
	dot_st = stat(dot_fn,&dot);

	sub_dir(dot_gale,"public-keys",0777);
	not_dot_fn = dir_file(dot_gale,auth_id_name(user_id));
	not_dot_st = stat(not_dot_fn,&not_dot);

	if (dot_st || not_dot_st ||
	    dot.st_dev != not_dot.st_dev ||
	    dot.st_ino != not_dot.st_ino) {
		gale_alert(GALE_NOTICE,"Updating ~/.gale-public-key.",0);
		unlink(dot_fn);
		if (link(not_dot_fn,dot_fn))
			gale_alert(GALE_WARNING,dot_fn,errno);
		else {
			int i = umask(0);
			umask(i);
			if (chmod(dot_fn,(0666 & ~i) | 0444)) 
				gale_alert(GALE_ERROR,dot_fn,errno);
		}
	}

	up_dir(dot_gale);
}

void old_gale_keys(void) {
	static int got_keys = 0;
	R_RANDOM_STRUCT rand;
	R_RSA_PROTO_KEY proto;
	int fd;

	if (got_keys) return;
	got_keys = 1;

	if (!read_pub_key(user_id,&pub_key) && 
	    !read_priv_key(user_id,&priv_key)) {
		check_dot_key();
		return;
	}

	gale_alert(GALE_NOTICE,
		"Generating RSA keys.  This takes time, but only once...",0);
	get_random(&rand);
	proto.bits = MAX_RSA_MODULUS_BITS;
	proto.useFermat4 = 1;
	if (R_GeneratePEMKeys(&pub_key,&priv_key,&proto,&rand)) 
		rsa_err("R_GeneratePEMKeys");
	gale_alert(GALE_NOTICE,"Done generating keys.",0);

	fd = write_file("private-keys",0700,auth_id_name(user_id),0600);
	priv_key.bits = htonl(priv_key.bits);
	write(fd,&priv_key,sizeof(priv_key));
	priv_key.bits = ntohl(priv_key.bits);
	close(fd);

	fd = write_file("public-keys",0777,auth_id_name(user_id),0666);
	pub_key.bits = htonl(pub_key.bits);
	write(fd,&pub_key,sizeof(pub_key));
	pub_key.bits = ntohl(pub_key.bits);
	close(fd);

	check_dot_key();
}

char *sign_data(struct gale_id *id,const char *data,const char *end) {
	R_SIGNATURE_CTX ctx;
	unsigned char *ptr,sig[MAX_SIGNATURE_LEN];
	char *ret;
	int i,len;
	gale_keys();

	ptr = (unsigned char *) data;
	if (R_SignInit(&ctx,DA_MD5)) rsa_err("R_SignInit");
	if (R_SignUpdate(&ctx,ptr,end - data)) rsa_err("R_SignUpdate");

	i = R_SignFinal(&ctx,sig,&len,&priv_key);
	if (i) rsa_err("R_SignFinal");

	i = strlen(auth_id_name(id));
	ret = gale_malloc(ENCODED_CONTENT_LEN(len) + i + 10);
	sprintf(ret,"RSA/MD5 %s ",auth_id_name(id));
	if (R_EncodePEMBlock(ret + i + 9,&len,sig,len)) 
		rsa_err("R_EncodePEMBlock");
	ret[i + 9 + len] = '\0';

	return ret;
}

void bad_sig(struct gale_id *id,char *msg) {
	char *tmp = gale_malloc(128 + strlen(auth_id_name(id)) + strlen(msg));
	sprintf(tmp,"CAN'T VALIDATE signature \"%s\"; %s",auth_id_name(id),msg);
	gale_alert(GALE_WARNING,tmp,0);
	gale_free(tmp);
}

struct gale_id *verify_data(const char *sig,const char *data,const char *end) {
	struct gale_id *id;
	char *cp,*dec;
	R_RSA_PUBLIC_KEY key;
	R_SIGNATURE_CTX ctx;
	int len,ret;

	gale_keys();
	if (strncmp(sig,"RSA/MD5 ",8)) {
		bad_sig(NULL,"unknown signature type");
		return NULL;
	}
	if (!(cp = strchr(sig + 8,' '))) {
		bad_sig(NULL,"invalid signature format");
		return NULL;
	}

	*cp = '\0';
	id = lookup_id(sig + 8);
	*cp = ' ';

	if (read_pub_key(id,&key)) {
		free_id(id);
		return NULL;
	}

	len = strlen(++cp);
	dec = gale_malloc(DECODED_CONTENT_LEN(len));
	ret = R_DecodePEMBlock(dec,&len,cp,len);
	if (ret == RE_ENCODING) {
		bad_sig(id,"invalid signature encoding");
		free_id(id);
		gale_free(dec);
		return NULL;
	}
	if (ret) rsa_err("R_DecodePEMBlock");

	if (R_VerifyInit(&ctx,DA_MD5)) rsa_err("R_VerifyInit");
	if (R_VerifyUpdate(&ctx,(char*)data,end - data)) 
		rsa_err("R_VerifyUpdate");
	ret = R_VerifyFinal(&ctx,dec,len,&key);
	gale_free(dec);
	switch (ret) {
	case 0: return id;
	case RE_LEN: 
		bad_sig(id,"invalid signature length");
		break;
	case RE_PUBLIC_KEY:
		bad_sig(id,"signature does not match key");
		break;
	case RE_SIGNATURE:
		bad_sig(id,"signature does not match message");
		break;
	default:
		rsa_err("R_VerifyFinal");
	}
	free_id(id);
	return NULL;
}

char *encrypt_data(struct gale_id *id,const char *data,const char *dend,
                      char *out,char **oend) 
{
	R_ENVELOPE_CTX ctx;
	unsigned char ekey[MAX_ENCRYPTED_KEY_LEN],*pekey = ekey,iv[8];
	unsigned int ekeylen,outlen,part;
	R_RSA_PUBLIC_KEY key,*pkey = &key;
	R_RANDOM_STRUCT rand;
	int len,enclen;
	char *tmp;

	gale_keys();

	if (read_pub_key(id,&key)) {
		char *tmp = gale_malloc(128 + strlen(auth_id_name(id)));
		sprintf(tmp,"Can't find key \"%s\"\r\n",auth_id_name(id));
		gale_alert(GALE_ERROR,tmp,0);
	}
	get_random(&rand);
	if (R_SealInit(&ctx,&pekey,&ekeylen,iv,1,&pkey,EA_DES_EDE3_CBC,&rand))
		rsa_err("R_SealInit");
	if (R_SealUpdate(&ctx,out,&outlen,(char*) data,dend - data))
		rsa_err("R_SealUpdate");
	if (R_SealFinal(&ctx,out + outlen,&part))
		rsa_err("R_SealFinal");
	*oend = out + outlen + part;

	len = strlen(auth_id_name(id));
	tmp = gale_malloc(12 + len + ENCODED_CONTENT_LEN(sizeof(iv)) + 
	                   ENCODED_CONTENT_LEN(ekeylen));
	sprintf(tmp,"RSA/3DES %s ",auth_id_name(id)); len += 10;
	if (R_EncodePEMBlock(tmp + len,&enclen,iv,sizeof(iv)))
		rsa_err("R_EncodePEMBlock");
	tmp[len + enclen] = ' '; len += enclen + 1;
	if (R_EncodePEMBlock(tmp + len,&enclen,ekey,ekeylen))
		rsa_err("R_EncodePEMBlock");
	tmp[len + enclen] = '\0'; len += enclen + 1;

	return tmp;
}

void bad_msg(struct gale_id *id,char *msg) {
	const char *name = id ? auth_id_name(id) : NULL;
	char *tmp;

	if (name) {
		tmp = gale_malloc(128 + strlen(name) + strlen(msg));
		sprintf(tmp,"CAN'T DECRYPT message to \"%s\"; %s",name,msg);
	} else {
		tmp = gale_malloc(128 + strlen(msg));
		sprintf(tmp,"CAN'T DECRYPT message; %s",msg);
	}

	gale_alert(GALE_WARNING,tmp,0);
	gale_free(tmp);
}

struct gale_id *decrypt_data(char *header,const char *data,const char *end,
                             char *out,char **oend)
{
	char *cp,*next;
	struct gale_id *id = NULL;
	unsigned char *iv = NULL,*ekey = NULL;
	int ivlen,ekeylen,ret,outlen,part;
	R_RSA_PRIVATE_KEY key;
	R_ENVELOPE_CTX ctx;

	if (strncmp(header,"RSA/3DES ",9)) {
		bad_msg(NULL,"unknown encryption type");
		goto error;
	}

	cp = header + 9;
	next = strchr(cp,' ');
	if (!next) {
		bad_msg(NULL,"invalid encryption format");
		goto error;
	}

	*next = '\0';
	id = lookup_id(cp);
	*next = ' ';

	if (read_priv_key(id,&key)) {
		bad_msg(id,"cannot find private key");
		goto error;
	}

	cp = next + 1;
	next = strchr(cp,' ');
	if (!next) {
		bad_msg(id,"invalid encryption format");
		goto error;
	}

	iv = gale_malloc(DECODED_CONTENT_LEN(next - cp));
	ret = R_DecodePEMBlock(iv,&ivlen,cp,next - cp);
	if (ret == RE_ENCODING) {
		bad_msg(id,"invalid encryption vector encoding");
		goto error;
	}
	if (ret) rsa_err("R_DecodePEMBlock");
	if (ivlen != 8) {
		bad_msg(id,"invalid encryption vector length");
		goto error;
	}

	cp = next + 1;
	next = cp + strlen(cp);
	ekey = gale_malloc(DECODED_CONTENT_LEN(next - cp));
	ret = R_DecodePEMBlock(ekey,&ekeylen,cp,next - cp);
	if (ret == RE_ENCODING) {
		bad_msg(id,"invalid encrypted key encoding");
		goto error;
	}
	if (ret) rsa_err("R_DecodePEMBlock");

	ret = R_OpenInit(&ctx,EA_DES_EDE3_CBC,ekey,ekeylen,iv,&key);
	switch (ret) {
	case 0: break;
	case RE_LEN: 
		bad_msg(id,"invalid encrypted key length");
		goto error;
	case RE_PRIVATE_KEY:
		bad_msg(id,"message does not match key");
		goto error;
	default:
		rsa_err("R_OpenInit");
	}

	if (R_OpenUpdate(&ctx,out,&outlen,(char *) data,end - data))
		rsa_err("R_OpenUpdate");
	if (R_OpenFinal(&ctx,out + outlen,&part))
		rsa_err("R_OpenFinal");
	*oend = out + outlen + part;

	gale_free(iv);
	gale_free(ekey);
	return id;

error:
	if (id) free_id(id);
	if (iv) gale_free(iv);
	if (ekey) gale_free(ekey);
	return NULL;
}
