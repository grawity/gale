#include <sys/utsname.h>
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

#include "gale/client.h"
#include "gale/util.h"
#include "gale/auth.h"

static R_RSA_PUBLIC_KEY pub_key;
static R_RSA_PRIVATE_KEY priv_key;

static void rsa_err(char *s) {
	fprintf(stderr,"auth: RSAREF failure in %s\r\n",s);
	exit(1);
}

static void get_random(R_RANDOM_STRUCT *rand) {
	R_DIGEST_CTX ctx;
	int needed,fd,len;
	char buf[MAX_DIGEST_LEN];

	fd = open("/dev/urandom",O_RDONLY);
	if (fd < 0) {
		struct stat buf;
		fd = open("random",O_RDONLY);
		if (fd >= 0 && (fstat(fd,&buf) || (buf.st_mode & 077))) {
			fprintf(stderr,
			"auth: The \".gale/random\" file cannot be "
				"readable by group or other.\n");
			if (fd >= 0) {
				close(fd);
				fd = -1;
			}
		}
		if (fd < 0) {
			fprintf(stderr,
			"auth: WARNING: You do not have a cryptographic "
				"random number source.\n"
			"Use a system with /dev/urandom (e.g. Linux) or "
				"otherwise get good random\n"
			"data into your \".gale/random\" file.\n");
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

	fd = open("random",O_CREAT | O_TRUNC | O_WRONLY,0600);
	if (fd >= 0) {
		write(fd,buf,len);
		close(fd);
	}
}

void gale_domain(const char **ptr) {
	static char *domain = NULL;
	static struct utsname uts;
	if (!domain) {
		domain = getenv("GALE_DOMAIN");
		if (!domain) {
			if (uname(&uts)) {
				perror("uname");
				exit(1);
			}
			domain = uts.nodename;
		}
	}
	*ptr = domain;
}

void gale_user(const char **ptr) {
	static char *user = NULL;
	if (!user) {
		user = getenv("USER");
		if (!user) {
			user = getenv("LOGNAME");
			if (!user) {
				fprintf(stderr,"auth: neither $LOGNAME "
				               "nor USER set\r\n");
				exit(1);
			}
		}
	}
	*ptr = user;
}

static char *canonicalize(const char *id) {
	const char *domain;
	char *tmp;
	if (strchr(id,'@')) return gale_strdup(id);
	gale_domain(&domain);
	tmp = gale_malloc(strlen(id) + strlen(domain) + 2);
	sprintf(tmp,"%s@%s",id,domain);
	return tmp;
}

void gale_id(const char **ptr) {
	static char *id = NULL;
	if (!id) {
		const char *tmp;
		tmp = getenv("GALE_ID");
		if (tmp)
			id = canonicalize(tmp);
		else {
			const char *domain,*user;
			gale_domain(&domain);
			gale_user(&user);
			id = gale_malloc(strlen(user) + strlen(domain) + 2);
			sprintf(id,"%s@%s",user,domain);
		}
	}
	*ptr = id;
}

static int read_file(const char *dir,int mode,const char *file) {
	int fd;
	gale_chdir();
	gale_subdir(dir,mode);
	fd = open(file,O_RDONLY);
	gale_unsubdir();
	if (fd < 0) {
		if (errno == ENOENT) return -1;
		perror(file);
		exit(1);
	}
	return fd;
}

static int read_pub_key(const char *id,R_RSA_PUBLIC_KEY *key) {
	int r,fd = read_file("public-keys",0777,id);
	if (fd < 0) return -1;
	r = read(fd,key,sizeof(*key));
	close(fd);
	if (r < sizeof(*key)) return -1;
	key->bits = ntohl(key->bits);
	return 0;
}

static int read_priv_key(const char *id,R_RSA_PRIVATE_KEY *key) {
	int r,fd = read_file("private-keys",0700,id);
	if (fd < 0) return -1;
	r = read(fd,key,sizeof(*key));
	close(fd);
	if (r < sizeof(*key)) return -1;
	key->bits = ntohl(key->bits);
	return 0;
}

static int write_file(const char *dir,int dmode,const char *file,int fmode) {
	int fd;
	gale_chdir();
	gale_subdir(dir,dmode);
	fd = creat(file,fmode);
	gale_unsubdir();
	if (fd < 0) {
		perror(file);
		exit(1);
	}
	return fd;
}

void gale_keys(void) {
	static int got_keys = 0;
	R_RANDOM_STRUCT rand;
	R_RSA_PROTO_KEY proto;
	const char *id,*home,*user,*domain;
	char *tmp;
	int fd,i;

	gale_id(&id);

	if (got_keys) return;
	got_keys = 1;

	gale_chdir();
	if (!read_pub_key(id,&pub_key) && !read_priv_key(id,&priv_key))
		return;

	fprintf(stderr,"auth: Generating RSA keys.  This takes time, "
	               "but we only do it once...\n");
	get_random(&rand);
	proto.bits = MAX_RSA_MODULUS_BITS;
	proto.useFermat4 = 1;
	if (R_GeneratePEMKeys(&pub_key,&priv_key,&proto,&rand)) 
		rsa_err("\nR_GeneratePEMKeys");
	fprintf(stderr,"auth: Done generating keys.\r\n");

	fd = write_file("private-keys",0700,id,0600);
	priv_key.bits = htonl(priv_key.bits);
	write(fd,&priv_key,sizeof(priv_key));
	priv_key.bits = ntohl(priv_key.bits);
	close(fd);

	fd = write_file("public-keys",0777,id,0666);
	pub_key.bits = htonl(pub_key.bits);
	write(fd,&pub_key,sizeof(pub_key));
	pub_key.bits = ntohl(pub_key.bits);
	close(fd);

	gale_user(&user);
	gale_domain(&domain);
	i = strlen(user);
	if (strncmp(user,id,i) || id[i] != '@' || strcmp(id+i+1,domain))
		return;

	home = getenv("HOME");
	if (home) {
		tmp = gale_malloc(strlen(home) + 20);
		sprintf(tmp,"%s/.gale-public-key",home);
		gale_subdir("public-keys",0777);
		unlink(tmp);
		if (link(id,tmp)) {
			perror(tmp);
			exit(1);
		}
		umask(i = umask(0));
		if (chmod(tmp,(0666 & ~i) | 0444)) {
			perror(tmp);
			exit(1);
		}
		gale_unsubdir();
		gale_free(tmp);
	}
}

static int find_key(const char *id,R_RSA_PUBLIC_KEY *key) {
	struct passwd *pwd;
	const char *cp,*domain;
	char *tmp;
	int fd,r;
	if (!read_pub_key(id,key)) return 0;
	cp = strrchr(id,'@');
	gale_domain(&domain);
	if (!cp || strcmp(cp + 1,domain)) return -1;
	tmp = gale_strndup(id,cp - id);
	pwd = getpwnam(tmp);
	gale_free(tmp);
	if (!pwd) return -1;
	tmp = gale_malloc(strlen(pwd->pw_dir) + 20);
	sprintf(tmp,"%s/.gale-public-key",pwd->pw_dir);
	fd = open(tmp,O_RDONLY);
	gale_free(tmp);
	if (fd < 0) return -1;
	r = read(fd,key,sizeof(*key));
	close(fd);
	if (r < sizeof(*key)) return -1;
	key->bits = ntohl(key->bits);
	return 0;
}

char *sign_message(const char *data,const char *end) {
	R_SIGNATURE_CTX ctx;
	unsigned char *ptr,sig[MAX_SIGNATURE_LEN];
	const char *id;
	char *ret;
	int i,len;
	gale_keys();

	ptr = (unsigned char *) data;
	if (R_SignInit(&ctx,DA_MD5)) rsa_err("R_SignInit");
	if (R_SignUpdate(&ctx,ptr,end - data)) rsa_err("R_SignUpdate");
	if (R_SignFinal(&ctx,sig,&len,&priv_key)) rsa_err("R_SignFinal");

	gale_id(&id); i = strlen(id);
	ret = gale_malloc(ENCODED_CONTENT_LEN(len) + i + 10);
	sprintf(ret,"RSA/MD5 %s ",id);
	if (R_EncodePEMBlock(ret + i + 9,&len,sig,len));
	ret[i + 9 + len] = '\0';
		
	return ret;
}

void bad_sig(char *id,char *msg) {
	fprintf(stderr,"auth: CANNOT VALIDATE signature \"%s\": %s\r\n",id,msg);
}

char *verify_signature(const char *sig,const char *data,const char *end) {
	char *id,*cp,*dec;
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
	id = gale_strndup(sig + 8,cp - (sig + 8));
	if (find_key(id,&key)) {
		gale_free(id);
		return NULL;
	}

	len = strlen(++cp);
	dec = gale_malloc(DECODED_CONTENT_LEN(len));
	ret = R_DecodePEMBlock(dec,&len,cp,len);
	if (ret == RE_ENCODING) {
		bad_sig(id,"invalid signature encoding");
		gale_free(id);
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
	gale_free(id);
	return NULL;
}

char *encrypt_message(char *id,const char *data,const char *dend,
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
	id = canonicalize(id);

	if (find_key(id,&key)) {
		fprintf(stderr,"auth: cannot find key \"%s\"\r\n",id);
		exit(1);
	}
	get_random(&rand);
	if (R_SealInit(&ctx,&pekey,&ekeylen,iv,1,&pkey,EA_DES_EDE3_CBC,&rand))
		rsa_err("R_SealInit");
	if (R_SealUpdate(&ctx,out,&outlen,(char*) data,dend - data))
		rsa_err("R_SealUpdate");
	if (R_SealFinal(&ctx,out + outlen,&part))
		rsa_err("R_SealFinal");
	*oend = out + outlen + part;

	len = strlen(id);
	tmp = gale_malloc(12 + len + ENCODED_CONTENT_LEN(sizeof(iv)) + 
	                   ENCODED_CONTENT_LEN(ekeylen));
	sprintf(tmp,"RSA/3DES %s ",id); len += 10;
	if (R_EncodePEMBlock(tmp + len,&enclen,iv,sizeof(iv)))
		rsa_err("R_EncodePEMBlock");
	tmp[len + enclen] = ' '; len += enclen + 1;
	if (R_EncodePEMBlock(tmp + len,&enclen,ekey,ekeylen))
		rsa_err("R_EncodePEMBlock");
	tmp[len + enclen] = '\0'; len += enclen + 1;

	gale_free(id);
	return tmp;
}

void bad_msg(char *id,char *msg) {
	fprintf(stderr,"auth: CANNOT DECRYPT message to \"%s\": %s\r\n",id,msg);
}

char *decrypt_message(char *header,const char *data,const char *end,
                      char *out,char **oend)
{
	char *cp,*next,*id = NULL;
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

	id = gale_strndup(cp,next - cp);
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
	if (id) gale_free(id);
	if (iv) gale_free(iv);
	if (ekey) gale_free(ekey);
	return NULL;
}
