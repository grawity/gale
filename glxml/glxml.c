#include "gale/all.h"

#include <parser.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

struct gale_encoding *utf8;

struct path {
	struct path *next;
	struct gale_text element;
};

void usage(void) {
	fprintf(stderr,"%s\n"
	"usage: cd logdir && glxml [-hn] category\n"
	"flags: -h          Display this message\n"
	"       -n          Do not fork\n",GALE_BANNER);
	exit(1);
}

struct gale_text format_date(struct gale_time t,int include_time) {
	struct timeval tv;
	char buf[256];

	gale_time_to(&tv,t);
	strftime(buf,sizeof(buf),
		include_time ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%d",
		gmtime(&tv.tv_sec));
	return gale_text_from(NULL,buf,-1);
}

static void i(FILE *fp,int indent) {
	while (indent-- > 0) fputc(' ',fp);
}

static void escape(FILE *fp,struct gale_text t) {
	const wch *p,* const e = t.p + t.l;
	for (p = t.p; e != p; ++p)
	switch (*p) {
	case '<': fputs("&lt;",fp); break;
	case '&': fputs("&amp;",fp); break;
	case '"': fputs("&quot;",fp); break;
	default: fputc(*p,fp);
	}
}

static void write_key(FILE *fp,int indent,struct auth_id *key,const char *n) {
	if (NULL == key) return;
	i(fp,indent); 
	fprintf(fp,"<%s>",n);
	escape(fp,auth_id_name(key));
	fprintf(fp,"</%s>\n",n);
}

struct path *split_name(struct gale_text name) {
	struct path *r;

	const wch * const e = name.p + name.l;
	while (e != name.p && !isalpha(*name.p)) ++name.p;
	if (e == name.p) return NULL;

	gale_create(r);
	r->element.p = name.p;
	do ++name.p; while (e != name.p && isalnum(*name.p));
	r->element.l = name.p - r->element.p;

	name.l = e - name.p;
	r->next = split_name(name);
	return r;
}

static void enter_path(FILE *fp,int *indent,struct path *p) {
	if (NULL == p) return;
	i(fp,*indent);
	fprintf(fp,"<f:%s",gale_text_to(utf8,p->element));
	if (NULL != p->next) fprintf(fp,">\n");
	*indent += 2;
	enter_path(fp,indent,p->next);
}

static void leave_path(FILE *fp,int *indent,struct path *p) {
	if (NULL == p) return;
	leave_path(fp,indent,p->next);
	*indent -= 2;
	if (NULL != p->next) i(fp,*indent);
	fprintf(fp,"</f:%s>\n",gale_text_to(utf8,p->element));
}

static struct path *set_path(
	FILE *fp,int *indent,
	struct path *old_path,struct gale_text new_text) 
{
	struct path *save_path,*new_path = split_name(new_text);
	if (0 != new_text.l && NULL == new_path) 
		new_path = split_name(G_("INVALID"));
	save_path = new_path;

	while (NULL != old_path && NULL != old_path->next
	   &&  NULL != new_path && NULL != new_path->next
	   && !gale_text_compare(old_path->element,new_path->element)) {
		old_path = old_path->next;
		new_path = new_path->next;
	}

	leave_path(fp,indent,old_path);
	enter_path(fp,indent,new_path);
	return save_path;
}

static void output_text(FILE *fp,int indent,struct gale_text t,int do_escape) {
	struct gale_text line = null_text;
	int has_lines = 0;
	while (gale_text_token(t,'\n',&line)) {
		struct gale_text trim = null_text;
		gale_text_token(line,'\r',&trim);
		if (0 == trim.l) continue;
		if (trim.p != t.p || trim.p + trim.l != t.p + t.l) {
			fputc('\n',fp);
			has_lines = 1;
		}
		if (do_escape)
			escape(fp,trim);
		else
			fputs(gale_text_to(utf8,trim),fp);
	}
	if (has_lines) {
		fputc('\n',fp);
		i(fp,indent - 2);
	}
}

static void write_text(FILE *fp,int indent,struct gale_text t) {
	xmlSAXHandler sax;
	xmlParserCtxtPtr ctxt;
	const char *sz = gale_text_to(utf8,t);
	const int len = strlen(sz);

	memset(&sax,'\0',sizeof(sax));
	ctxt = xmlCreatePushParserCtxt(&sax,NULL,NULL,0,"gale");
	output_text(fp,indent,t,
	   0 != xmlParseChunk(ctxt,"<puff>",6,0)
	|| 0 != xmlParseChunk(ctxt,sz,len,0)
	|| 0 != xmlParseChunk(ctxt,"</puff>",7,1));
	xmlFreeParserCtxt(ctxt);
}

static void write_data(FILE *fp,int indent,struct gale_data d) {
	int p;
	for (p = 0; p < d.l; ++p) {
		if (0 == (p%20)) {
			fputc('\n',fp);
			i(fp,indent);
		}
		fprintf(fp,"%02X ",d.p[p]);
	}
}

static void write_group(FILE *fp,int indent,struct gale_group gr) {
	struct path *last = NULL;
	while (!gale_group_null(gr)) {
		struct gale_fragment f = gale_group_first(gr);
		last = set_path(fp,&indent,last,f.name);
		switch (f.type) {
		case frag_text: 
			fputs(" type='text'>",fp);
			write_text(fp,indent,f.value.text);
			break;
		case frag_data: 
			fputs(" type='data'>",fp); 
			write_data(fp,indent,f.value.data);
			fputc('\n',fp);
			i(fp,indent);
			break;
		case frag_time: 
			fputs(" type='time'>",fp); 
			escape(fp,format_date(f.value.time,1));
			break;
		case frag_number: 
			fputs(" type='number'>",fp); 
			escape(fp,gale_text_from_number(f.value.number,10,0));
			break;
		case frag_group: 
			fputs(" type='group'>\n",fp); 
			write_group(fp,indent,f.value.group);
			i(fp,indent);
			break;
		}
		gr = gale_group_rest(gr);
	}
	set_path(fp,&indent,last,null_text);
}

static void *on_message(struct gale_link *l,struct gale_packet *pkt,void *x) {
	struct old_gale_message *msg = gale_receive(pkt);
	struct gale_text tok = null_text;
	const char * const trailer = "\n</g:log>\n";
	const int trailer_len = strlen(trailer);
	char buffer[1024] = "";

	struct gale_time when_received = gale_time_now();
	struct auth_id *who_signed = auth_verify(&msg->data);
	struct gale_text name = format_date(when_received,0);
	const char *fn = gale_text_to(gale_global->enc_filesys,name);
	FILE *fp = fopen(fn,"r+");
	if (NULL == fp) fp = fopen(fn,"w+");
	if (NULL == fp) {
		gale_alert(GALE_WARNING,name,errno);
		return OOP_CONTINUE;
	}

	if (0 != fseek(fp,-trailer_len,SEEK_END))
		fprintf(fp,"<g:log\n"
		"  xmlns:g='http://gale.org/xml/1.0'\n"
		"  xmlns:f='http://gale.org/xml/1.0/fragment'>\n\n");
	else if (1 != fread(buffer,trailer_len,1,fp)
	     ||  EOF != fgetc(fp)
	     ||  0 != strcmp(buffer,trailer)
	     ||  0 != fseek(fp,-trailer_len,SEEK_END)) {
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("corrupted file: \""),name,G_("\"")),0);
		fclose(fp);
		return OOP_CONTINUE;
	}

	fprintf(fp,"<g:message>\n  <g:time-received>%s</g:time-received>\n",
	        gale_text_to(utf8,format_date(when_received,1)));

	fprintf(fp,"  <g:categories>\n");
	while (gale_text_token(msg->cat,':',&tok)) {
		fprintf(fp,"    <g:category>");
		escape(fp,tok);
		fprintf(fp,"</g:category>\n");
	}
	fprintf(fp,"  </g:categories>\n");

	write_key(fp,2,who_signed,"g:signed-by");
	write_group(fp,2,msg->data);

	fprintf(fp,"</g:message>\n");
	fputs(trailer,fp);
	fclose(fp);
	return OOP_CONTINUE;
}

int main(int argc,char *argv[]) {
	oop_source_sys *sys;
	oop_source *source;
	struct gale_text subs;
	struct gale_link *link;
	struct gale_server *server;
	int arg,do_fork = 1;

	gale_init("glxml",argc,argv);
	gale_init_signals(source = oop_sys_source(sys = oop_sys_new()));
	utf8 = gale_make_encoding(G_("UTF-8"));

	while ((arg = getopt(argc,argv,"hn")) != EOF) 
	switch (arg) {
	case 'n': do_fork = 0; break;
	case 'h':
	case '?': usage();
	}
	if (1 + optind != argc) usage();
	subs = gale_text_from(gale_global->enc_cmdline,argv[optind],-1);

	link = new_link(source);
	link_on_message(link,on_message,NULL);
	gale_set_error_link(source,link);
	server = gale_open(source,link,subs,null_text,0);

	if (do_fork) {
		char buf[PATH_MAX] = "(error)";
		gale_daemon(source);
		getcwd(buf,PATH_MAX);
		gale_kill(gale_text_from(gale_global->enc_filesys,buf,-1),1);
		gale_detach();
	}

	oop_sys_run(sys);

	return 0;
}
