#ifndef GALE_DIR_H
#define GALE_DIR_H

extern struct gale_dir *dot_gale,*home_dir;

const char *dir_file(struct gale_dir *,const char *);

struct gale_dir *dup_dir(struct gale_dir *);
struct gale_dir *make_dir(const char *,int mode);
void free_dir(struct gale_dir *);

void sub_dir(struct gale_dir *,const char *,int mode);
void up_dir(struct gale_dir *);

#endif
