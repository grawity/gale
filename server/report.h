#ifndef REPORT_H
#define REPORT_H

struct report;
typedef struct gale_text report_call(void *);

struct report *make_report(void);
void report_add(struct report *,report_call *,void *);
void report_remove(struct report *,report_call *,void *);
struct gale_text report_run(struct report *);

#endif
